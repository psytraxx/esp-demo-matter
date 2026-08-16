#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Buffer sizes for the QR payload ("MT:...") and manual pairing code strings
// returned by matter_get_pairing_codes(). Shared so callers size their stack
// buffers to match what the Matter stack actually writes.
#define MATTER_QR_BUF_LEN      256
#define MATTER_MANUAL_CODE_LEN 32

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the Matter RGB light endpoint, register the commissioning event
// callback, and start the Matter stack. boot_events/commissioned_bit are used
// to signal MATTER_COMMISSIONING_COMPLETE and server_ready_bit for
// kServerReady.
void matter_setup(EventGroupHandle_t boot_events,
                   EventBits_t commissioned_bit,
                   EventBits_t server_ready_bit);

// True if the device has a commissioned fabric in NVS.
bool matter_is_commissioned(void);

// Toggle the light endpoint's On/Off attribute and push the new value to
// Matter — the same on/off state Home Assistant controls. Called from the
// BOOT button's short-press handler, so the light can be switched either way.
void matter_button_toggle(void);

// Fill qr_buf with the "MT:…" QR payload and code_buf with the manual pairing
// code. Blocks until the commissioning window is open and the codes are ready;
// returns immediately if the device is already commissioned.
void matter_get_pairing_codes(char *qr_buf,  size_t qr_len,
                               char *code_buf, size_t code_len);

// Trigger a factory reset (clears fabric, reopens commissioning window).
void matter_factory_reset(void);

#ifdef __cplusplus
}
#endif
