#include "ld2410_bridge.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

extern "C" {
#include "ld2410c.h"
}
#include "app_config.h"
#include "board_pins.h"

static const char *TAG = "ld2410_bridge";

static radar_presence_cb_t s_on_change = NULL;
static ld2410c_handle_t    *s_dev      = NULL;

static esp_err_t radar_uart_init(void)
{
    uart_config_t cfg = {};
    cfg.baud_rate      = RADAR_UART_BAUD_RATE;
    cfg.data_bits      = UART_DATA_8_BITS;
    cfg.parity         = UART_PARITY_DISABLE;
    cfg.stop_bits      = UART_STOP_BITS_1;
    cfg.flow_ctrl      = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk     = UART_SCLK_DEFAULT;

    esp_err_t err = uart_driver_install(RADAR_UART_PORT, 256, 0, 0, NULL, 0);
    if (err != ESP_OK)
        return err;

    err = uart_param_config(RADAR_UART_PORT, &cfg);
    if (err != ESP_OK)
        return err;

    return uart_set_pin(RADAR_UART_PORT, RADAR_UART_TX, RADAR_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// Polls the sensor and reports presence changes.
//
// The LD2410 streams a target-data frame roughly every 100 ms, so
// ld2410c_read_data_frame() (which blocks for up to RADAR_READ_TIMEOUT_MS
// waiting for one complete frame) doubles as the poll period — no extra
// delay needed between iterations.
//
// The hold ("keep reporting occupied for a while after the last detection")
// is implemented here rather than on the sensor: unlike the previous driver,
// ld2410c has no sensor-side "no-one window" setting, so a target-lost frame
// only clears the reported state once RADAR_NO_ONE_WINDOW_S has passed since
// the last frame that showed a target. This absorbs both momentary frame
// gaps and brief dips in the sensor's own per-frame detection.
static void radar_task(void *)
{
    ESP_LOGI(TAG, "LD2410 radar ready (no-one window %lu s)", RADAR_NO_ONE_WINDOW_S);

    bool    reported_occupied = false;
    int64_t last_target_us    = 0;
    uint8_t frame_buf[128];

    for (;;)
    {
        size_t    frame_len = 0;
        esp_err_t err       = ld2410c_read_data_frame(s_dev, frame_buf, sizeof(frame_buf), &frame_len);

        if (err == ESP_OK)
        {
            ld2410c_target_data_t data;
            if (ld2410c_parse_target_data(frame_buf, frame_len, &data) == ESP_OK &&
                data.state != LD2410C_TARGET_NONE)
                last_target_us = esp_timer_get_time();
        }

        const bool occupied = last_target_us != 0 &&
                               (esp_timer_get_time() - last_target_us) < (int64_t)RADAR_NO_ONE_WINDOW_S * 1000000;

        if (occupied != reported_occupied)
        {
            reported_occupied = occupied;
            if (s_on_change)
                s_on_change(occupied);
        }
    }
}

esp_err_t radar_bridge_init(radar_presence_cb_t on_change)
{
    s_on_change = on_change;

    esp_err_t err = radar_uart_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "radar UART init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_dev = ld2410c_init(RADAR_UART_PORT, RADAR_READ_TIMEOUT_MS);
    if (!s_dev)
    {
        ESP_LOGE(TAG, "ld2410c_init failed");
        return ESP_ERR_NO_MEM;
    }

    // 3072 matches button_task's stack for a similarly small polling loop.
    if (xTaskCreate(radar_task, "radar", 3072, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "radar task create failed");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
