#include "matter_setup.h"

#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <algorithm>

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_matter.h"
#include "esp_matter_endpoint.h"
#include "esp_matter_cluster.h"
#include "esp_matter_attribute.h"
#include "freertos/portmacro.h"

#include "app_config.h"

// Thread (C6) transport: the CHIP OpenThread launcher asserts unless it has been
// handed a platform config before esp_matter::start() brings the Thread stack up.
#if CONFIG_OPENTHREAD_ENABLED
#include "esp_openthread.h"
#include "esp_openthread_types.h"
#include <platform/ESP32/OpenthreadLauncher.h>

#define OT_DEFAULT_RADIO_CONFIG()                                                    \
    {                                                                                \
        .radio_mode = RADIO_MODE_NATIVE,                                             \
    }
#define OT_DEFAULT_HOST_CONFIG()                                                     \
    {                                                                                \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                           \
    }
#define OT_DEFAULT_PORT_CONFIG()                                                     \
    {                                                                                \
        .storage_partition_name = "nvs", .netif_queue_size = 10,                     \
        .task_queue_size = 10,                                                        \
    }
#endif // CONFIG_OPENTHREAD_ENABLED

// Matter/CHIP stack headers
#include <app/server/Server.h>
#include <app/server/CommissioningWindowManager.h>
#include <lib/core/CHIPError.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <system/SystemClock.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Attributes.h>

#include "status_led.h"
#include "board_pins.h"

static const char *TAG = "matter_setup";
// Commissioning window timeout (s)
static constexpr uint16_t COMMISSIONING_WINDOW_TIMEOUT_S = 300;

// Sentinel for "endpoint not yet created" — 0xFFFF is the Matter wildcard/invalid ID.
static constexpr uint16_t MATTER_EP_INVALID = 0xFFFFu;

// Endpoint IDs, populated on create
static uint16_t s_ep_light  = MATTER_EP_INVALID; // extended_color_light (WS2812 RGB)

static EventGroupHandle_t s_boot_events      = NULL;
static EventBits_t        s_commissioned_bit = 0;
static EventBits_t        s_server_ready_bit = 0;

// Pairing code buffers written from the Matter event callback
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static char  s_qr_code[MATTER_QR_BUF_LEN]         = {};
static char  s_manual_code[MATTER_MANUAL_CODE_LEN] = {};
static EventGroupHandle_t s_matter_events = NULL;
#define MATTER_BIT_CODES_READY (1 << 0)

// ── Helpers ───────────────────────────────────────────────────────────────────

// C6 tickless light sleep (see CLAUDE.md) otherwise engages between BLE GATT
// exchanges, and the resulting wake latency stacks up over a commissioning
// session until the commissioner (phone/HA) gives up and drops the link. Hold
// the radio awake and the CPU at full speed for the duration of the BLE
// commissioning window so pairing stays responsive.
static esp_pm_lock_handle_t s_commissioning_pm_lock = NULL;
static bool                 s_commissioning_pm_lock_held = false;

static void commissioning_pm_lock_acquire(void)
{
    if (!s_commissioning_pm_lock)
    {
        esp_err_t err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0,
                                            "ble_commission", &s_commissioning_pm_lock);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "esp_pm_lock_create failed: %d", err);
            return;
        }
    }
    if (!s_commissioning_pm_lock_held)
    {
        esp_pm_lock_acquire(s_commissioning_pm_lock);
        s_commissioning_pm_lock_held = true;
        ESP_LOGI(TAG, "PM: light sleep held off for BLE commissioning");
    }
}

static void commissioning_pm_lock_release(void)
{
    if (s_commissioning_pm_lock_held)
    {
        esp_pm_lock_release(s_commissioning_pm_lock);
        s_commissioning_pm_lock_held = false;
        ESP_LOGI(TAG, "PM: light sleep re-enabled");
    }
}

static void open_commissioning_window(void)
{
    auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (mgr.IsCommissioningWindowOpen())
        return;

    CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds16(COMMISSIONING_WINDOW_TIMEOUT_S),
        chip::CommissioningWindowAdvertisement::kAllSupported);
    if (err != CHIP_NO_ERROR)
        ESP_LOGE(TAG, "OpenBasicCommissioningWindow: %" CHIP_ERROR_FORMAT, err.Format());
}

static void refresh_pairing_codes(void)
{
    char manual[sizeof(s_manual_code)] = {};
    char qr[sizeof(s_qr_code)]         = {};

    chip::RendezvousInformationFlags flags(chip::RendezvousInformationFlag::kBLE);

    chip::MutableCharSpan manual_span(manual);
    if (GetManualPairingCode(manual_span, flags) == CHIP_NO_ERROR)
        manual[manual_span.size()] = '\0';

    chip::MutableCharSpan qr_span(qr);
    if (GetQRCode(qr_span, flags) == CHIP_NO_ERROR)
        qr[qr_span.size()] = '\0';

    portENTER_CRITICAL(&s_mux);
    memcpy(s_manual_code, manual, sizeof(s_manual_code));
    memcpy(s_qr_code, qr, sizeof(s_qr_code));
    portEXIT_CRITICAL(&s_mux);

    if (s_matter_events)
        xEventGroupSetBits(s_matter_events, MATTER_BIT_CODES_READY);

    ESP_LOGI(TAG, "Matter manual code: %s", manual);
    ESP_LOGI(TAG, "Matter QR payload : %s", qr);
}

// ── Matter event callback ───────────────────────────────────────────────────

static void matter_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t /*arg*/)
{
    if (!event)
        return;

    switch (event->Type)
    {
    case chip::DeviceLayer::DeviceEventType::kServerReady:
        ESP_LOGI(TAG, "Matter server ready");
        if (s_boot_events && s_server_ready_bit)
            xEventGroupSetBits(s_boot_events, s_server_ready_bit);
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0)
        {
            ESP_LOGI(TAG, "Device already commissioned");
            if (s_boot_events)
                xEventGroupSetBits(s_boot_events, s_commissioned_bit);
        }
        else
        {
            open_commissioning_window();
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        commissioning_pm_lock_acquire();
        refresh_pairing_codes();
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        commissioning_pm_lock_release();
        if (s_boot_events)
            xEventGroupSetBits(s_boot_events, s_commissioned_bit);
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Commissioning failed (failsafe expired)");
        commissioning_pm_lock_release();
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed — reopening commissioning window");
        open_commissioning_window();
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized after commissioning");
        break;

    default:
        break;
    }
}

// ── Light state → WS2812 RGB translation ────────────────────────────────────

// The extended_color_light endpoint carries On/Off, Level Control (brightness,
// 0-254) and Color Control (XY color space) as independent attributes. Track
// the last value of each here so any single attribute write can recompute the
// final RGB output.
static bool    s_light_on    = false;
static uint8_t s_light_level = 254;    // CurrentLevel default (full brightness)
// CurrentX/CurrentY defaults match esp_matter's color_control::feature::xy
// config_t defaults (0x616b/0x607d — a warm-white point), not 0/0. Tracking
// the wrong default here left the LED black on the very first On command,
// before Home Assistant had ever written a color.
static uint16_t s_light_x    = 0x616b; // CurrentX (0..65535 represents 0.0..1.0)
static uint16_t s_light_y    = 0x607d; // CurrentY

// CIE 1931 xyY → sRGB (Philips Hue formula), Y normalised to 1.0 — brightness
// is applied separately from the Level Control attribute.
static void xy_to_rgb(float x, float y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (y <= 0.0f)
    {
        *r = *g = *b = 0;
        return;
    }

    float z = 1.0f - x - y;
    float Y = 1.0f;
    float X = (Y / y) * x;
    float Z = (Y / y) * z;

    float rf =  X * 1.656492f - Y * 0.354851f - Z * 0.255038f;
    float gf = -X * 0.707196f + Y * 1.655397f + Z * 0.036152f;
    float bf =  X * 0.051713f - Y * 0.121364f + Z * 1.011530f;

    auto gamma = [](float c) -> float {
        c = std::max(0.0f, std::min(1.0f, c));
        return (c <= 0.0031308f) ? 12.92f * c
                                  : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
    };
    rf = gamma(rf);
    gf = gamma(gf);
    bf = gamma(bf);

    float maxc = std::max({rf, gf, bf, 1.0f});
    rf /= maxc; gf /= maxc; bf /= maxc;

    *r = (uint8_t)lroundf(std::max(0.0f, std::min(1.0f, rf)) * 255.0f);
    *g = (uint8_t)lroundf(std::max(0.0f, std::min(1.0f, gf)) * 255.0f);
    *b = (uint8_t)lroundf(std::max(0.0f, std::min(1.0f, bf)) * 255.0f);
}

static void apply_light_state(void)
{
    if (!s_light_on)
    {
        status_led_set_rgb(0, 0, 0);
        return;
    }

    uint8_t r, g, b;
    xy_to_rgb((float)s_light_x / 65536.0f, (float)s_light_y / 65536.0f, &r, &g, &b);

    float scale = (float)s_light_level / 254.0f;
    status_led_set_rgb((uint8_t)lroundf(r * scale),
                        (uint8_t)lroundf(g * scale),
                        (uint8_t)lroundf(b * scale));
}

// ── Attribute update callback (fired by Matter stack on attribute writes) ───

static esp_err_t attr_update_cb(esp_matter::attribute::callback_type_t type,
                                 uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t attribute_id, esp_matter_attr_val_t *val,
                                 void * /*priv_data*/)
{
    using namespace chip::app::Clusters;

    if (type != esp_matter::attribute::POST_UPDATE)
        return ESP_OK;

    if (endpoint_id == s_ep_light)
    {
        bool changed = true;
        if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id)
            s_light_on = val->val.b;
        else if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id)
            s_light_level = val->val.u8;
        else if (cluster_id == ColorControl::Id && attribute_id == ColorControl::Attributes::CurrentX::Id)
            s_light_x = val->val.u16;
        else if (cluster_id == ColorControl::Id && attribute_id == ColorControl::Attributes::CurrentY::Id)
            s_light_y = val->val.u16;
        else
            changed = false;

        if (changed)
            apply_light_state();
    }

    return ESP_OK;
}

static esp_err_t identify_cb(esp_matter::identification::callback_type_t /*type*/,
                              uint16_t /*ep*/, uint8_t /*effect*/, uint8_t /*variant*/,
                              void * /*priv_data*/)
{
    return ESP_OK;
}

// ── Endpoint creation ────────────────────────────────────────────────────────

static esp_err_t create_endpoints(esp_matter::node_t *node)
{
    using namespace esp_matter;

    {
        endpoint::extended_color_light::config_t cfg = {};
        endpoint_t *ep = endpoint::extended_color_light::create(node, &cfg, ENDPOINT_FLAG_NONE, NULL);
        if (!ep)
        {
            ESP_LOGE(TAG, "extended_color_light create failed");
            return ESP_FAIL;
        }
        s_ep_light = endpoint::get_id(ep);
    }

    ESP_LOGI(TAG, "Endpoints: light=%u", s_ep_light);
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────

extern "C" void matter_setup(EventGroupHandle_t boot_events,
                              EventBits_t commissioned_bit,
                              EventBits_t server_ready_bit)
{
    s_boot_events      = boot_events;
    s_commissioned_bit = commissioned_bit;
    s_server_ready_bit = server_ready_bit;
    s_matter_events    = xEventGroupCreate();

    using namespace esp_matter;

    node::config_t node_cfg = {};
    node_t *node = node::create(&node_cfg, attr_update_cb, identify_cb);
    if (!node)
    {
        ESP_LOGE(TAG, "node::create failed — restarting");
        esp_restart();
    }

    if (create_endpoints(node) != ESP_OK)
    {
        ESP_LOGE(TAG, "Endpoint creation failed — restarting");
        esp_restart();
    }

#if CONFIG_OPENTHREAD_ENABLED
    // Hand the OpenThread launcher its platform config before the stack starts,
    // otherwise openthread_init_stack() asserts on a null s_platform_config.
    static esp_openthread_platform_config_t ot_config = {
        .radio_config = OT_DEFAULT_RADIO_CONFIG(),
        .host_config  = OT_DEFAULT_HOST_CONFIG(),
        .port_config  = OT_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
#endif

    ESP_ERROR_CHECK(esp_matter::start(matter_event_cb));
    ESP_LOGI(TAG, "Matter stack started");

    // Name the board in the controller UI.
    {
        char label[33] = {};
        strncpy(label, BOARD_NODE_LABEL, sizeof(label) - 1);
        attribute_t *attr = attribute::get(
            0, chip::app::Clusters::BasicInformation::Id,
            chip::app::Clusters::BasicInformation::Attributes::NodeLabel::Id);
        if (attr)
        {
            esp_matter_attr_val_t val =
                esp_matter_char_str(label, (uint16_t)strlen(label));
            esp_err_t err = attribute::set_val(attr, &val);
            if (err != ESP_OK)
                ESP_LOGW(TAG, "NodeLabel set failed: %s", esp_err_to_name(err));
            else
                ESP_LOGI(TAG, "NodeLabel: %s", label);
        }
    }
}

extern "C" bool matter_is_commissioned(void)
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

extern "C" void matter_button_toggle(void)
{
    using namespace esp_matter;
    using namespace chip::app::Clusters;

    if (s_ep_light == MATTER_EP_INVALID)
        return;

    esp_matter_attr_val_t val = esp_matter_bool(false);
    if (attribute::get_val(s_ep_light, OnOff::Id, OnOff::Attributes::OnOff::Id, &val) != ESP_OK)
        return;

    val.val.b = !val.val.b;
    attribute::update(s_ep_light, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
    ESP_LOGI(TAG, "Light toggled: %s", val.val.b ? "on" : "off");
}

extern "C" void matter_get_pairing_codes(char *qr_buf,  size_t qr_len,
                                          char *code_buf, size_t code_len)
{
    // Wait up to 30 s for the commissioning window to open and codes to be ready.
    if (s_matter_events)
    {
        xEventGroupWaitBits(s_matter_events, MATTER_BIT_CODES_READY,
                            pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    }

    portENTER_CRITICAL(&s_mux);
    if (qr_buf && qr_len > 0)
    {
        strlcpy(qr_buf, s_qr_code, qr_len);
    }
    if (code_buf && code_len > 0)
    {
        strlcpy(code_buf, s_manual_code, code_len);
    }
    portEXIT_CRITICAL(&s_mux);
}

extern "C" void matter_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset requested");
    esp_matter::factory_reset();
}
