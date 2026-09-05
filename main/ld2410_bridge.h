#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*radar_presence_cb_t)(bool occupied);

// Starts the LD2410 radar (jef-sure/ld2410c component) and its polling task.
// Blocks briefly while the component confirms the sensor responds.
// on_change fires only on a genuine edge of the debounced presence state
// (the sensor's own no-one window, see RADAR_NO_ONE_WINDOW_S in
// app_config.h), never per poll.
esp_err_t radar_bridge_init(radar_presence_cb_t on_change);

#ifdef __cplusplus
}
#endif
