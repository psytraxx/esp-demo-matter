#pragma once

#include "driver/gpio.h"

// ─────────────────────────────────────────────────────────────────────────────
// Single source of truth for every GPIO used by this demo, on the Waveshare
// ESP32-C6-LCD-1.3 (display unused — only the onboard button and RGB LED).
// ─────────────────────────────────────────────────────────────────────────────

// Onboard
#define PIN_STATUS_LED  GPIO_NUM_8 // WS2812 RGB; also a strapping pin
#define PIN_WAKE_BUTTON GPIO_NUM_9 // BOOT button; long hold = factory reset; strapping pin
