#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "app_config.h"
#include "matter_setup.h"
#include "button.h"
#include "status_led.h"

static const char *TAG = "app_main";

// Boot event bits — set by subsystems when they reach a ready state.
#define BOOT_BIT_COMMISSIONED  (1 << 0)
#define BOOT_BIT_SERVER_READY  (1 << 1)

static EventGroupHandle_t g_boot_events = NULL;

static void app_init();
static bool run_commissioning();

// ── BOOT button callback (runs in the button task context) ──────────────────

static void on_button_long_press(void)
{
    ESP_LOGW(TAG, "Factory reset!");
    matter_factory_reset();
}

static void on_button_short_press(void)
{
    // Toggles the light endpoint's On/Off attribute — the same state Home
    // Assistant controls, so the LED can be switched either way. No separate
    // status blink here: that would briefly overwrite the light's own color.
    matter_button_toggle();
}


// One-time hardware and subsystem initialisation.
static void app_init()
{
    g_boot_events = xEventGroupCreate();
    configASSERT(g_boot_events);

    status_led_set(STATUS_LED_BOOT);

    matter_setup(g_boot_events, BOOT_BIT_COMMISSIONED, BOOT_BIT_SERVER_READY);

    // Interrupt-driven BOOT button — short press toggles the light's On/Off
    // attribute, long hold factory-resets. Blocks while idle so it does not
    // defeat light sleep.
    button_init(on_button_long_press, on_button_short_press);
}

// Run the commissioning flow when the device is not yet paired. Blocks until
// MATTER_COMMISSIONING_COMPLETE is signalled via the boot EventGroup (fabric
// committed to NVS). Returns true if a fresh commission happened this boot.
static bool run_commissioning()
{
    if (matter_is_commissioned())
        return false;

    status_led_set(STATUS_LED_COMMISSIONING);

    char qr_buf[MATTER_QR_BUF_LEN]        = {};
    char code_buf[MATTER_MANUAL_CODE_LEN] = {};
    matter_get_pairing_codes(qr_buf, sizeof(qr_buf), code_buf, sizeof(code_buf));

    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "  MATTER COMMISSIONING — device not yet paired (Thread)");
    ESP_LOGI(TAG, "  Pair over BLE; a Thread Border Router must be present.");
    ESP_LOGI(TAG, "  Manual pairing code : %s", code_buf);
    ESP_LOGI(TAG, "  QR payload          : %s", qr_buf);
    ESP_LOGI(TAG, "============================================================");

    // Wait for MATTER_COMMISSIONING_COMPLETE (fabric committed to NVS), not just
    // isDeviceCommissioned() which fires ~5 s earlier on "fabric updated" before
    // the NVS flush. HA opens a second commissioning window if we proceed too early.
    // 10-minute guard prevents an indefinite hang if the event is never delivered.
    const TickType_t timeout = pdMS_TO_TICKS(10UL * 60UL * 1000UL);
    EventBits_t bits = xEventGroupWaitBits(g_boot_events, BOOT_BIT_COMMISSIONED,
                                            pdFALSE, pdTRUE, timeout);
    if (!(bits & BOOT_BIT_COMMISSIONED))
    {
        ESP_LOGE(TAG, "Commissioning timeout — restarting to retry");
        status_led_set(STATUS_LED_ERROR);
        esp_restart();
    }

    // Do NOT restart here. HA performs the device interview (reads every cluster)
    // and establishes the operational CASE session over Thread immediately after
    // commissioning, all on the live device.
    ESP_LOGI(TAG, "Commissioning complete — joining Thread, staying live for HA interview");
    status_led_set(STATUS_LED_OK);
    return true;
}

extern "C" void app_main(void)
{
    esp_log_level_set("BLE_INIT", ESP_LOG_WARN);

    ESP_LOGI(TAG, "=== Matter demo device boot ===");

    app_init();

    run_commissioning();

    // Wait for the Matter server to finish init (kServerReady) — attribute
    // writes before this point return INVALID_STATE.
    xEventGroupWaitBits(g_boot_events, BOOT_BIT_SERVER_READY,
                        pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));

    // Don't clear the LED here — matter_setup() already restored it to the
    // persisted on/off + color state during Matter stack start.
    ESP_LOGI(TAG, "Ready — button toggles the light's On/Off state, LED follows it");
}
