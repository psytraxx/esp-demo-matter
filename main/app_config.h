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

// The LD2410's "no-one window": it keeps reporting a target for this many
// seconds after the last detection before declaring the room empty. This is a
// sensor-side setting (written once at startup), not a software debounce —
// the sensor decides using per-gate energy data the firmware never sees.
// Raise it if the occupancy sensor clears while someone is sitting still.
inline constexpr uint8_t RADAR_NO_ONE_WINDOW_S = 5;

// The LD2410 streams target frames within a second of power-on, but does not
// answer commands (which is how the driver's begin() confirms it is present)
// until several seconds in. Wait this long before the first begin() attempt,
// and this long again between retries, or startup reliably fails.
inline constexpr uint32_t RADAR_WARMUP_MS      = 5000;
inline constexpr uint32_t RADAR_RETRY_DELAY_MS = 2000;
