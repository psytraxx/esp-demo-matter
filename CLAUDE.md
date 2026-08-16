# CLAUDE.md

Minimal Matter test device for the **Waveshare ESP32-C6-LCD-1.3** (display
unused). Language: C++ on the **native ESP-IDF v5.5.5** toolchain
(`espressif/esp_matter`), same stack as the sibling `esp32-homecontrol-matter`
project — see that project's CLAUDE.md for the full rationale behind the
Matter-over-Thread ICD LIT power model, light-sleep quirks, and commissioning
flow; this file only covers what's specific to this stripped-down demo.

One Matter endpoint — **Extended Color Light**, driving the onboard WS2812 RGB
LED (GPIO8). Home Assistant shows a full-color light with a color wheel, a
colour-temperature tab and a brightness slider; the device type mandates the
temperature control, so it is implemented (blackbody → RGB) rather than left
dangling, and the light renders whichever of the two colour controls was
written last. The same light can also be switched on/off with the
physical BOOT button (GPIO9, short press) — both control paths toggle the
same On/Off attribute, so the button and Home Assistant always agree on
state. Long-press (5 s) factory-resets.

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

## Changelog — Always Update

Every change to this project gets an entry in `CHANGELOG.md`, in the same
commit as the change itself. Entries are grouped by date, newest first, under
`### Added` / `### Changed` / `### Fixed` headings.

Write entries for someone using the device, not someone reading the diff:
describe the behaviour that changed and why it mattered, in plain language,
without file or function names. Compare the existing entries for the tone —
"the light now resumes its last colour after a power cycle", not "call
`apply_light_state()` after `esp_matter::start()`".

## Platform Quirks — Do Not Break

Same ICD LIT / tickless-light-sleep model as `esp32-homecontrol-matter`: no
task may busy-poll. The BOOT button is interrupt-driven with a GPIO
light-sleep wake source (`button_init()`); don't reintroduce polling.

`StartUpOnOff` and `StartUpCurrentLevel` must stay **null** in the
`extended_color_light` config (`create_endpoints()`). esp_matter defaults both
to `0`, which the Matter spec reads as "power up Off" and "power up at level 0"
(clamped to `min_level` = 1) — that silently overwrites the persisted light
state on every boot. Null selects "restore previous value", which is what makes
the light come back as it was left.

Light state is restored from the cluster's own non-volatile attributes
(On/Off, CurrentLevel, CurrentX/Y, ColorTemperatureMireds and **ColorMode**) —
don't add a parallel NVS store for any of it. ColorMode is maintained by the
Matter colour-control server and tells the restore path whether the light was
last driven by the colour wheel or the temperature slider.

The colour fade (`status_led_fade_rgb()`) runs in a task that exits as soon as
it reaches the target, so it only holds the CPU awake for the length of the
transition. Keep it that way — a permanently resident animation task would
defeat light sleep.
