#include "button.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "app_config.h"

static const char *TAG = "button";

static void (*s_on_long_press)(void)  = NULL;
static void (*s_on_short_press)(void) = NULL;
static TaskHandle_t s_task = NULL;

// ISR: only unblocks the button task. All real work (logging, Matter/display
// calls) happens in task context where it is safe. The interrupt is
// level-triggered (see button_init for why), so mask this line immediately —
// otherwise it would re-fire continuously while the button is held down. The
// task re-arms it once the button is released.
static void IRAM_ATTR button_isr(void *)
{
    gpio_intr_disable(PIN_WAKE_BUTTON);
    BaseType_t hpw = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &hpw);
    portYIELD_FROM_ISR(hpw);
}

// Blocks until a press wakes it, then times how long the button is held: a
// hold of FACTORY_RESET_HOLD_MS or longer fires the long-press callback, a
// shorter press fires the short-press callback. Because it blocks (rather than
// polls) while idle, it does not keep the CPU out of tickless light sleep.
static void button_task(void *)
{
    const TickType_t step = pdMS_TO_TICKS(50);
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Debounce: confirm the button is genuinely down. If it was a spurious
        // blip, re-arm the (now masked) interrupt and wait for the next press.
        vTaskDelay(pdMS_TO_TICKS(20));
        if (gpio_get_level(PIN_WAKE_BUTTON) != 0)
        {
            gpio_intr_enable(PIN_WAKE_BUTTON);
            continue;
        }

        TickType_t press_start = xTaskGetTickCount();
        bool       fired_long  = false;
        while (gpio_get_level(PIN_WAKE_BUTTON) == 0)
        {
            vTaskDelay(step);
            uint32_t held_ms = (xTaskGetTickCount() - press_start) * portTICK_PERIOD_MS;
            if (!fired_long && held_ms >= FACTORY_RESET_HOLD_MS)
            {
                fired_long = true;
                ESP_LOGW(TAG, "BOOT held %" PRIu32 " ms — long press", held_ms);
                if (s_on_long_press)
                    s_on_long_press();
            }
        }

        uint32_t held_ms = (xTaskGetTickCount() - press_start) * portTICK_PERIOD_MS;
        if (!fired_long && held_ms > 0 && s_on_short_press)
        {
            ESP_LOGI(TAG, "BOOT short press (%" PRIu32 " ms)", held_ms);
            s_on_short_press();
        }

        // Button released — re-arm the level interrupt for the next press.
        gpio_intr_enable(PIN_WAKE_BUTTON);
    }
}

void button_init(void (*on_long_press)(void), void (*on_short_press)(void))
{
    s_on_long_press  = on_long_press;
    s_on_short_press = on_short_press;

    // Level-triggered (LOW), NOT edge-triggered. The chip wakes from automatic
    // tickless light sleep on the LOW level (see below), but with peripherals
    // powered down in light sleep the falling *edge* is consumed by the wake
    // logic and never latches a GPIO edge interrupt — so an edge-triggered ISR
    // would silently miss every press made while the device is asleep (the
    // common case for a commissioned device). A level interrupt instead fires
    // as soon as the GPIO peripheral is restored and sees the pin still held
    // LOW. The ISR masks itself and the task re-arms it on release.
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_WAKE_BUTTON),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_LOW_LEVEL,
    };
    gpio_config(&cfg);

    // Task must exist before the ISR can notify it.
    if (xTaskCreate(button_task, "button", 3072, NULL, 5, &s_task) != pdPASS)
    {
        ESP_LOGE(TAG, "button task create failed — factory-reset button disabled");
        return;
    }

    // gpio_install_isr_service returns INVALID_STATE if already installed; both
    // outcomes are fine, so don't ESP_ERROR_CHECK it.
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_WAKE_BUTTON, button_isr, NULL);

    // Let a press wake the chip from automatic light sleep so the ISR can run.
    gpio_wakeup_enable(PIN_WAKE_BUTTON, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    ESP_LOGI(TAG, "BOOT button ready (hold %" PRIu32 " ms to factory-reset)",
             (uint32_t)FACTORY_RESET_HOLD_MS);
}
