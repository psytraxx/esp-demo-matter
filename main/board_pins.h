#pragma once

#include "driver/gpio.h"

// ─────────────────────────────────────────────────────────────────────────────
// Single source of truth for every GPIO used by this demo, on the Waveshare
// ESP32-C6-LCD-1.3 (display unused — only the onboard button and RGB LED).
// ─────────────────────────────────────────────────────────────────────────────

// Onboard
#define PIN_STATUS_LED  GPIO_NUM_8 // WS2812 RGB; also a strapping pin
#define PIN_WAKE_BUTTON GPIO_NUM_9 // BOOT button; long hold = factory reset; strapping pin

// LD2410 24 GHz presence radar: pins are set via the `esp32-ld2410` component's
// own Kconfig (LD2410_UART_RX / LD2410_UART_TX), not here — see
// sdkconfig.defaults. Currently IO2/IO3 (left header). NOT IO16/IO17: despite
// being broken out, those are this board's console UART0 pins and conflict
// with it.
