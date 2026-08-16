#include "status_led.h"

#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"

static const char *TAG = "status_led";

#define STATUS_BRIGHTNESS 16  // dim; used only for the fixed boot/commissioning/error states
#define RMT_RESOLUTION_HZ 10000000U  // 10 MHz → 100 ns per tick

static rmt_channel_handle_t s_rmt_chan = NULL;
static rmt_encoder_handle_t s_rmt_enc  = NULL;

// WS2812B timing at 10 MHz (1 tick = 100 ns):
//   T0H = 4 ticks (400 ns), T0L = 9 ticks (900 ns)
//   T1H = 8 ticks (800 ns), T1L = 4 ticks (400 ns)
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void ensure_init(void)
{
    portENTER_CRITICAL(&s_mux);
    if (s_rmt_chan)
    {
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    static bool init_started = false;
    if (init_started)
    {
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    init_started = true;
    portEXIT_CRITICAL(&s_mux);

    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num          = PIN_STATUS_LED,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags             = {
            .invert_out       = false,
            .with_dma         = false,
            .io_loop_back     = false,
            .io_od_mode       = false,
        },
    };
    if (rmt_new_tx_channel(&chan_cfg, &s_rmt_chan) != ESP_OK)
    {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed");
        return;
    }
    rmt_enable(s_rmt_chan);

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .duration0 = 4, .level0 = 1,  // T0H
            .duration1 = 9, .level1 = 0,  // T0L
        },
        .bit1 = {
            .duration0 = 8, .level0 = 1,  // T1H
            .duration1 = 4, .level1 = 0,  // T1L
        },
        .flags = { .msb_first = 1 },
    };
    if (rmt_new_bytes_encoder(&enc_cfg, &s_rmt_enc) != ESP_OK)
    {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder failed");
        rmt_disable(s_rmt_chan);
        rmt_del_channel(s_rmt_chan);
        s_rmt_chan = NULL;
    }
}

void status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ensure_init();
    if (!s_rmt_chan || !s_rmt_enc)
        return;

    // WS2812B byte order is G, R, B
    uint8_t grb[3] = {g, r, b};
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags = { .eot_level = 0 },  // hold LOW after tx — acts as WS2812B reset (>50 µs)
    };
    esp_err_t err = rmt_transmit(s_rmt_chan, s_rmt_enc, grb, sizeof(grb), &tx_cfg);
    if (err == ESP_OK)
        rmt_tx_wait_all_done(s_rmt_chan, pdMS_TO_TICKS(10));
    else
        ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
}

void status_led_set(status_led_state_t state)
{
    switch (state)
    {
    case STATUS_LED_BOOT:
        for (int i = 0; i < 3; i++)
        {
            status_led_set_rgb(STATUS_BRIGHTNESS, STATUS_BRIGHTNESS, STATUS_BRIGHTNESS);  // white on
            vTaskDelay(pdMS_TO_TICKS(100));
            status_led_set_rgb(0, 0, 0);                                                   // off
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        break;
    case STATUS_LED_COMMISSIONING: status_led_set_rgb(0, 0, STATUS_BRIGHTNESS); break;  // blue
    case STATUS_LED_OK:
        status_led_set_rgb(0, STATUS_BRIGHTNESS, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        status_led_set_rgb(0, 0, 0);
        break;
    case STATUS_LED_ERROR:         status_led_set_rgb(STATUS_BRIGHTNESS, 0, 0); break;  // red
    case STATUS_LED_OFF:
    default:                       status_led_set_rgb(0, 0, 0); break;
    }
}
