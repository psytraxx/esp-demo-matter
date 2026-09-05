#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

// ─────────────────────────────────────────────────────────────────────────────
// Single source of truth for every GPIO used by this demo, on the Waveshare
// ESP32-C6-LCD-1.3 (display unused — only the onboard button and RGB LED).
// ─────────────────────────────────────────────────────────────────────────────

// Onboard
#define PIN_STATUS_LED  GPIO_NUM_8 // WS2812 RGB; also a strapping pin
#define PIN_WAKE_BUTTON GPIO_NUM_9 // BOOT button; long hold = factory reset; strapping pin

// LD2410 24 GHz presence radar (UART1). Only IO1/IO2/IO3, IO12/13/16/17/20/23
// are broken out on this board — see docs/image.png. IO16/IO17 were the first
// choice but are wrong: this board's console (CONFIG_ESP_CONSOLE_UART) runs on
// UART0 at those same default pins, which crash-looped the boot when the radar
// driver also claimed them. IO2/IO3 are on the same left header as 5V/GND, and
// are not strapping pins (GPIO4/5/8/9/15 are, per the ESP32-C6 datasheet) or
// the console UART.
#define RADAR_UART_PORT UART_NUM_1
#define RADAR_UART_TX   GPIO_NUM_3
#define RADAR_UART_RX   GPIO_NUM_2
