#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Per-build Matter commissioning identity.
//
// Without this file every flashed device uses the CHIP SDK's shared test setup
// values (passcode 20202021 / discriminator 0xF00) and therefore prints the
// same pairing code. Loaded into the CHIP build via CONFIG_CHIP_PROJECT_CONFIG
// in sdkconfig.defaults.
//
// To give each device its own pairing code, change BOTH values below before
// flashing it:
//   1. Pick a new discriminator (any value 0x000–0xFFF; must differ between
//      devices that are commissioned at the same time).
//   2. Pick a new passcode (1–99999998, not a trivial value like 12345678)
//      and regenerate the matching verifier:
//          python3 tools/spake2p_verifier.py <passcode>
//      The passcode and verifier MUST be kept in sync — a mismatch bricks
//      commissioning silently.
// ─────────────────────────────────────────────────────────────────────────────

#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR 0x820

#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE 20250816

// Generated with: python3 tools/spake2p_verifier.py 20250816
#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_VERIFIER                        \
    "XpKnXq3eEW4XiHgXR/LzHTwR+3yQGzV94Mhfl13VIBMEce0olq+vi/2U/+s2qAZl4oLt" \
    "+91YIrfcWIra2Zefvm2aFSNWSW3n5FajUI/gVLv7BqxL677r81EA39f5th8xAQ=="

// ─────────────────────────────────────────────────────────────────────────────
// Basic Information cluster identity, shown by controllers (e.g. Home
// Assistant's "Device info" panel). Without these the CHIP SDK defaults to
// TEST_VENDOR / TEST_PRODUCT / TEST_VERSION.
// ─────────────────────────────────────────────────────────────────────────────

#include "sdkconfig.h"

#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "psytraxx"

// Serial number shown in the controller's device info (SDK default: TEST_SN).
// Like the pairing code above, make it unique per device before flashing.
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION 1
#define CHIP_DEVICE_CONFIG_TEST_SERIAL_NUMBER "DEMOMATTER-0001"
// Hardware identity; the numeric version lets controllers tell board
// revisions apart if the wiring ever changes.
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "Waveshare ESP32-C6-LCD-1.3"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "Matter Light Demo"

// Firmware version string is deliberately NOT overridden: the SDK default is
// the git describe of the build, which is exactly what we want to see against
// a running device.
