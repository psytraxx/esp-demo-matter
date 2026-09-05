#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// Tunable application constants. Pin assignments live in board_pins.h.
// ─────────────────────────────────────────────────────────────────────────────

// Name this device reports to Matter controllers (Home Assistant shows it as
// the device name). Max 32 chars per the Matter spec.
inline constexpr const char *BOARD_NODE_LABEL = "Matter Light Demo";

// Hold the BOOT button (PIN_WAKE_BUTTON) this long to factory-reset.
inline constexpr uint32_t FACTORY_RESET_HOLD_MS = 5000;

// The radar's "no-one window": presence keeps being reported for this many
// seconds after the last frame with a target, so a momentary gap in frames
// (or someone briefly holding still enough to drop below the sensor's noise
// floor for one reading) doesn't flicker the reported state. Software-side
// debounce — the ld2410c driver has no equivalent sensor-side setting, unlike
// the previous driver. Raise it if occupancy clears while someone is still
// in the room.
inline constexpr uint32_t RADAR_NO_ONE_WINDOW_S = 5;

// The LD2410 starts streaming target frames within about a second of power-on,
// so no warm-up delay is needed before the first read.
inline constexpr uint32_t RADAR_READ_TIMEOUT_MS = 1000;

// Sensor's factory-default UART settings (256000 baud, 8N1).
inline constexpr uint32_t RADAR_UART_BAUD_RATE = 256000;
