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
