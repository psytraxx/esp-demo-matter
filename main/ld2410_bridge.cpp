#include "ld2410_bridge.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h" // UART teardown between ld2410_begin() retries

// ld2410.h has no extern "C" guard of its own and is compiled as plain C in
// the component (ld2410.c) — wrap the include here so this .cpp file links
// against the component's unmangled C symbols instead of C++-mangled ones.
extern "C" {
#include "ld2410.h"
}
#include "app_config.h"

static const char *TAG = "ld2410_bridge";

static radar_presence_cb_t s_on_change = NULL;
static LD2410_device_t    *s_dev       = NULL;

// Brings the sensor online, then polls it. Runs entirely in its own task so
// the multi-second warm-up below never blocks boot or commissioning.
//
// The sensor needs several seconds after power-on before it answers commands,
// even though it starts streaming target frames much earlier. Calling
// ld2410_begin() straight away reliably times out; the same call succeeds
// first time once the sensor has had ~5 s to settle. (Diagnosed by a passive
// UART probe that happened to introduce exactly that delay — frames were
// arriving cleanly at 256000 baud the whole time, so this is a sensor
// readiness issue, not wiring, baud, or a stale RX FIFO.)
static bool radar_bring_online(void)
{
    vTaskDelay(pdMS_TO_TICKS(RADAR_WARMUP_MS));

    for (int attempt = 1; attempt <= 4; attempt++)
    {
        if (ld2410_begin(s_dev))
            return true;

        ESP_LOGW(TAG, "LD2410 begin attempt %d failed — retrying in %d ms",
                 attempt, (int)RADAR_RETRY_DELAY_MS);
        // begin() calls ESP_ERROR_CHECK(uart_driver_install(...)), which aborts
        // if the driver is already installed, so tear it down between attempts.
        uart_driver_delete((uart_port_t)CONFIG_LD2410_UART_PORT_NUM);
        vTaskDelay(pdMS_TO_TICKS(RADAR_RETRY_DELAY_MS));
    }
    return false;
}

// Polls the sensor and debounces its raw presence reading: occupied is
// reported the instant a target is seen, but only cleared after RADAR_HOLD_MS
// with no target, so the reported occupancy doesn't chatter between polls.
static void radar_task(void *)
{
    if (!radar_bring_online())
    {
        ESP_LOGE(TAG, "LD2410 did not respond — radar sensor disabled");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "LD2410 radar ready");

    bool     reported_occupied = false;
    bool     raw_occupied      = false;
    TickType_t clear_deadline  = 0;

    for (;;)
    {
        ld2410_check(s_dev);
        raw_occupied = ld2410_presence_detected(s_dev);

        if (raw_occupied)
        {
            if (!reported_occupied)
            {
                reported_occupied = true;
                if (s_on_change)
                    s_on_change(true);
            }
        }
        else if (reported_occupied)
        {
            TickType_t now = xTaskGetTickCount();
            if (clear_deadline == 0)
                clear_deadline = now + pdMS_TO_TICKS(RADAR_HOLD_MS);
            else if (now >= clear_deadline)
            {
                reported_occupied = false;
                clear_deadline    = 0;
                if (s_on_change)
                    s_on_change(false);
            }
        }

        if (raw_occupied)
            clear_deadline = 0;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t radar_bridge_init(radar_presence_cb_t on_change)
{
    s_on_change = on_change;

    s_dev = ld2410_new();
    if (!s_dev)
    {
        ESP_LOGE(TAG, "ld2410_new failed");
        return ESP_ERR_NO_MEM;
    }

    // No general task-stack Kconfig in this component (LD2410_OUT_PIN_TASK_STACK_SIZE
    // only applies to its own optional OUT-pin ISR handler, which is unused here);
    // 3072 matches button_task's stack for a similarly small polling loop.
    // The task does the sensor handshake itself, so this returns immediately and
    // "ready" is logged from the task once the sensor actually answers.
    if (xTaskCreate(radar_task, "radar", 3072, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "radar task create failed");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
