# CLAUDE.md

Minimal Matter test device for the **Waveshare ESP32-C6-LCD-1.3** (display
unused). Language: C++ on the **native ESP-IDF v5.5.5** toolchain
(`espressif/esp_matter`), same stack as the sibling `esp32-homecontrol-matter`
project — see that project's CLAUDE.md for the full rationale behind the
Matter-over-Thread ICD LIT power model, light-sleep quirks, and commissioning
flow; this file only covers what's specific to this stripped-down demo.

Two Matter endpoints:
- **Extended Color Light** (endpoint driving the onboard WS2812 RGB LED,
  GPIO8) — Home Assistant shows a full-color light with a color wheel and
  brightness slider.
- **On/Off Plug-in Unit** (`main/button.cpp` BOOT button, GPIO9) — each short
  press toggles this endpoint's On/Off attribute; Home Assistant shows it as a
  switch. Long-press (5 s) factory-resets.

No display: the manual pairing code and QR payload are printed to the serial
console only (`run_commissioning()` in `main/app_main.cpp`).

## Build Commands

```bash
source ~/.espressif/v5.5.5/esp-idf/export.sh
idf.py build
idf.py flash monitor
```

## Code Organisation

| File | Purpose |
|------|---------|
| `main/app_config.h` | Node label, factory-reset hold time |
| `main/board_pins.h` | GPIO pin assignments (LED, button) |
| `main/matter_setup.cpp` / `.h` | Endpoint creation, XY→RGB translation, commissioning event handler, pairing-code printing |
| `main/status_led.cpp` / `.h` | WS2812 RMT driver; `status_led_set_rgb()` for arbitrary color, `status_led_set()` for fixed boot/commissioning/error states |
| `main/button.cpp` / `.h` | Interrupt-driven BOOT button (unmodified from the sibling project) |
| `main/app_main.cpp` | `app_main()` entry point, boot/commissioning sequence |
| `main/chip_project_config.h` | Per-build pairing code (discriminator/passcode/verifier) — distinct from the sibling project so both can be commissioned on the same fabric |

## Platform Quirks — Do Not Break

Same ICD LIT / tickless-light-sleep model as `esp32-homecontrol-matter`: no
task may busy-poll. The BOOT button is interrupt-driven with a GPIO
light-sleep wake source (`button_init()`); don't reintroduce polling.
