#pragma once

#include <stdint.h>

// Status-indicator states, driven by the onboard WS2812B addressable LED
// (see status_led.cpp). Used for boot/commissioning/error indication before
// Home Assistant has set a color.
typedef enum
{
    STATUS_LED_OFF,            // idle / no active status
    STATUS_LED_BOOT,           // device alive, boot in progress
    STATUS_LED_COMMISSIONING,  // commissioning window open
    STATUS_LED_OK,             // brief blink acknowledging an event
    STATUS_LED_ERROR,          // error indication
} status_led_state_t;

#ifdef __cplusplus
extern "C" {
#endif

void status_led_set(status_led_state_t state);

// Drive the LED to an arbitrary RGB color (0-255 per channel), held until the
// next call. This is what the Matter Extended Color Light endpoint's
// Color Control writes are translated into. Takes effect immediately.
void status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

// As status_led_set_rgb(), but fades from the current color over
// STATUS_LED_FADE_MS instead of jumping. A fade already in progress is
// retargeted, so rapid updates (a Home Assistant colour-wheel drag) chase the
// latest value rather than queueing up. The fade runs in a short-lived task
// that exits once the target is reached.
void status_led_fade_rgb(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif
