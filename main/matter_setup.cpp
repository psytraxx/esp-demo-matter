#include "matter_setup.h"

#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <algorithm>

#include "esp_log.h"
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
static uint16_t s_ep_light      = MATTER_EP_INVALID; // extended_color_light (WS2812 RGB)
static uint16_t s_ep_occupancy  = MATTER_EP_INVALID; // occupancy_sensor (LD2410 radar)

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
        refresh_pairing_codes();
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        if (s_boot_events)
            xEventGroupSetBits(s_boot_events, s_commissioned_bit);
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Commissioning failed (failsafe expired)");
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

// extended_color_light always exposes ColorTemperature alongside XY (the device
// type mandates it), so Home Assistant shows a temperature tab either way.
// s_light_use_temp mirrors the cluster's own ColorMode attribute, which the
// colour-control server maintains and stores non-volatile.
static uint16_t s_light_temp_mireds = 250; // ~4000 K
static bool     s_light_use_temp    = false;

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

// Mireds (10^6 / kelvin) → sRGB, via Tanner Helland's blackbody approximation.
// Output is normalised to full brightness; the Level Control attribute scales it.
static void mireds_to_rgb(uint16_t mireds, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (mireds == 0)
    {
        *r = *g = *b = 0;
        return;
    }

    float kelvin = 1000000.0f / (float)mireds;
    kelvin = std::max(1000.0f, std::min(40000.0f, kelvin));
    float t = kelvin / 100.0f;

    auto clamp255 = [](float v) -> uint8_t {
        return (uint8_t)lroundf(std::max(0.0f, std::min(255.0f, v)));
    };

    float rf, gf, bf;
    if (t <= 66.0f)
    {
        rf = 255.0f;
        gf = 99.4708025861f * logf(t) - 161.1195681661f;
        bf = (t <= 19.0f) ? 0.0f : 138.5177312231f * logf(t - 10.0f) - 305.0447927307f;
    }
    else
    {
        rf = 329.698727446f * powf(t - 60.0f, -0.1332047592f);
        gf = 288.1221695283f * powf(t - 60.0f, -0.0755148492f);
        bf = 255.0f;
    }

    *r = clamp255(rf);
    *g = clamp255(gf);
    *b = clamp255(bf);
}

// fade == true smoothly transitions from the colour currently shown; false
// snaps straight to it (used on boot, where there is nothing to fade from).
static void apply_light_state(bool fade)
{
    if (!s_light_on)
    {
        if (fade)
            status_led_fade_rgb(0, 0, 0);
        else
            status_led_set_rgb(0, 0, 0);
        return;
    }

    uint8_t r, g, b;
    if (s_light_use_temp)
        mireds_to_rgb(s_light_temp_mireds, &r, &g, &b);
    else
        xy_to_rgb((float)s_light_x / 65536.0f, (float)s_light_y / 65536.0f, &r, &g, &b);

    float scale = (float)s_light_level / 254.0f;
    uint8_t out_r = (uint8_t)lroundf(r * scale);
    uint8_t out_g = (uint8_t)lroundf(g * scale);
    uint8_t out_b = (uint8_t)lroundf(b * scale);

    if (fade)
        status_led_fade_rgb(out_r, out_g, out_b);
    else
        status_led_set_rgb(out_r, out_g, out_b);
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
        {
            s_light_x = val->val.u16;
            s_light_use_temp = false;
        }
        else if (cluster_id == ColorControl::Id && attribute_id == ColorControl::Attributes::CurrentY::Id)
        {
            s_light_y = val->val.u16;
            s_light_use_temp = false;
        }
        else if (cluster_id == ColorControl::Id &&
                 attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id)
        {
            s_light_temp_mireds = val->val.u16;
            s_light_use_temp = true;
        }
        // ColorMode is maintained by the Matter colour-control server itself and
        // persisted across reboots; mirror it so the restore path and the live
        // path agree on which colour control is active.
        else if (cluster_id == ColorControl::Id && attribute_id == ColorControl::Attributes::ColorMode::Id)
            s_light_use_temp = (val->val.u8 == (uint8_t)ColorControl::ColorModeEnum::kColorTemperatureMireds);
        else
            changed = false;

        if (changed)
            apply_light_state(true);
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

        // Matter spec: StartUpOnOff / StartUpCurrentLevel of *null* means
        // "restore the previous value on power-up". esp_matter defaults both to
        // 0 instead, which means "come up Off" and (clamped by min_level=1)
        // "come up at brightness 1" — so every power cycle overwrote the
        // persisted state with a dark, minimum-brightness light.
        cfg.on_off_lighting.start_up_on_off = nullable<uint8_t>();
        cfg.level_control_lighting.start_up_current_level = nullable<uint8_t>();

        // Advertise a normal lamp's color-temperature range (2000 K–6535 K).
        // The esp_matter defaults leave the physical min/max at the full
        // theoretical span, which makes Home Assistant draw a mostly-flat
        // gradient with a red band instead of a warm-to-cool ramp.
        cfg.color_control_color_temperature.color_temp_physical_min_mireds = 153; // ~6535 K
        cfg.color_control_color_temperature.color_temp_physical_max_mireds = 500; // ~2000 K
        cfg.color_control_color_temperature.color_temperature_mireds       = 250; // ~4000 K

        // Same trap as StartUpOnOff: esp_matter defaults this to a concrete
        // value (250 mireds) rather than null. The spec says a non-null
        // StartUpColorTemperatureMireds forces ColorMode to "colour
        // temperature" on every boot — which threw away a restored XY colour
        // and lit the LED warm white instead. Null means "keep the previous
        // value", leaving ColorMode alone.
        cfg.color_control_color_temperature.start_up_color_temperature_mireds = nullable<uint16_t>();

        endpoint_t *ep = endpoint::extended_color_light::create(node, &cfg, ENDPOINT_FLAG_NONE, NULL);
        if (!ep)
        {
            ESP_LOGE(TAG, "extended_color_light create failed");
            return ESP_FAIL;
        }
        s_ep_light = endpoint::get_id(ep);
    }

    {
        using namespace chip::app::Clusters;

        // occupancy_sensor_type[_bitmap] are the legacy (pre-1.4) attributes,
        // whose enum has no radar value — kPir is the closest and what Home
        // Assistant expects for an occupancy binary_sensor. feature_flags is
        // the newer (Matter 1.4) Feature bitmap, which does have kRadar; the
        // cluster's create() hard-asserts if feature_flags carries none of
        // its recognised bits, so this is not optional.
        endpoint::occupancy_sensor::config_t cfg = {};
        cfg.occupancy_sensing.occupancy_sensor_type =
            chip::to_underlying(OccupancySensing::OccupancySensorTypeEnum::kPir);
        cfg.occupancy_sensing.occupancy_sensor_type_bitmap =
            chip::to_underlying(OccupancySensing::OccupancySensorTypeBitmap::kPir);
        cfg.occupancy_sensing.feature_flags =
            chip::to_underlying(OccupancySensing::Feature::kRadar);

        endpoint_t *ep = endpoint::occupancy_sensor::create(node, &cfg, ENDPOINT_FLAG_NONE, NULL);
        if (!ep)
        {
            ESP_LOGE(TAG, "occupancy_sensor create failed");
            return ESP_FAIL;
        }
        s_ep_occupancy = endpoint::get_id(ep);
    }

    ESP_LOGI(TAG, "Endpoints: light=%u occupancy=%u", s_ep_light, s_ep_occupancy);
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

    // esp_matter restores persisted attribute values from NVS into its
    // internal store on start(), but that restore does not go through
    // attr_update_cb (no POST_UPDATE fires), so our s_light_* cache and the
    // physical LED would otherwise stay at their power-on defaults until the
    // next attribute write. Pull the actual values back out here so the LED
    // resumes the last on/off + color state across reboots.
    {
        using namespace chip::app::Clusters;
        esp_matter_attr_val_t val = esp_matter_bool(false);

        if (attribute::get_val(s_ep_light, OnOff::Id, OnOff::Attributes::OnOff::Id, &val) == ESP_OK)
            s_light_on = val.val.b;
        if (attribute::get_val(s_ep_light, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &val) == ESP_OK)
            s_light_level = val.val.u8;
        if (attribute::get_val(s_ep_light, ColorControl::Id, ColorControl::Attributes::CurrentX::Id, &val) == ESP_OK)
            s_light_x = val.val.u16;
        if (attribute::get_val(s_ep_light, ColorControl::Id, ColorControl::Attributes::CurrentY::Id, &val) == ESP_OK)
            s_light_y = val.val.u16;
        if (attribute::get_val(s_ep_light, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id, &val) == ESP_OK)
            s_light_temp_mireds = val.val.u16;

        // ColorMode (spec: 2 = colour temperature) records which control the
        // light was last driven by. The colour-control server maintains it and
        // it is stored non-volatile, so it survives the power cycle with the
        // rest of the light state.
        if (attribute::get_val(s_ep_light, ColorControl::Id, ColorControl::Attributes::ColorMode::Id, &val) == ESP_OK)
            s_light_use_temp = (val.val.u8 == (uint8_t)ColorControl::ColorModeEnum::kColorTemperatureMireds);

        // The config default above only applies to a fresh NVS. On a device
        // that was flashed with an earlier build, StartUpColorTemperatureMireds
        // is already stored as 250, and the colour-control server re-forces
        // ColorMode to "colour temperature" on every boot from it — discarding
        // the restored XY colour. Write the null back explicitly so the stored
        // value stops overriding the previous colour.
        {
            esp_matter_attr_val_t null_temp =
                esp_matter_nullable_uint16(nullable<uint16_t>());
            attribute::update(s_ep_light, ColorControl::Id,
                              ColorControl::Attributes::StartUpColorTemperatureMireds::Id,
                              &null_temp);
        }

        apply_light_state(false);
        ESP_LOGI(TAG, "Restored light state: on=%d level=%u x=%u y=%u mireds=%u temp_mode=%d",
                 s_light_on, s_light_level, s_light_x, s_light_y,
                 s_light_temp_mireds, s_light_use_temp);
    }

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

extern "C" void matter_report_occupancy(bool occupied)
{
    using namespace esp_matter;
    using namespace chip::app::Clusters;

    if (s_ep_occupancy == MATTER_EP_INVALID)
        return;

    esp_matter_attr_val_t val = esp_matter_bitmap8(occupied ? 0x01 : 0x00);
    attribute::update(s_ep_occupancy, OccupancySensing::Id,
                      OccupancySensing::Attributes::Occupancy::Id, &val);
    ESP_LOGI(TAG, "Occupancy reported: %s", occupied ? "occupied" : "clear");
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
