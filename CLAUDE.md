# CLAUDE.md

Minimal Matter test device for the **Waveshare ESP32-C6-LCD-1.3** (display
unused). Language: C++ on the **native ESP-IDF v5.5.5** toolchain
(`espressif/esp_matter`), the same stack as the sibling
`esp32-homecontrol-matter` project — see that project's CLAUDE.md for the
commissioning flow rationale; this file only covers what's specific to this
stripped-down demo.

**Power model differs from the sibling project.** That one is a battery-minded
sensor node running Matter ICD LIT as a sleepy end device. This is a
mains-powered light, so it runs as an always-on Thread **FTD** with no ICD and
no light sleep: a sleepy device would add up to a poll interval of latency
before it reacted to Home Assistant, which is the wrong trade for a light.
Don't port the ICD/light-sleep configuration back over from the sibling.

One Matter endpoint — **Extended Color Light**, driving the onboard WS2812 RGB
LED (GPIO8). Home Assistant shows a full-color light with a color wheel, a
colour-temperature tab and a brightness slider; the device type mandates the
temperature control, so it is implemented (blackbody → RGB) rather than left
dangling, and the light renders whichever of the two colour controls was
written last. The same light can also be switched on/off with the
physical BOOT button (GPIO9, short press) — both control paths toggle the
same On/Off attribute, so the button and Home Assistant always agree on
state. Long-press (5 s) factory-resets. Colour changes fade rather than snap,
and the full light state (on/off, brightness, colour) is restored after a power
cut — see the `StartUp*` note below, which is what makes that work.

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
| `main/matter_setup.cpp` / `.h` | Endpoint creation, XY→RGB and mireds→RGB translation, light-state restore, commissioning event handler, pairing-code printing |
| `main/status_led.cpp` / `.h` | WS2812 RMT driver; `status_led_set_rgb()` sets a color immediately, `status_led_fade_rgb()` transitions to it, `status_led_set()` for fixed boot/commissioning/error states |
| `main/button.cpp` / `.h` | Interrupt-driven BOOT button (adapted from the sibling project; its light-sleep wake source was dropped along with ICD) |
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

The device is always on — no ICD, no tickless light sleep (see the power-model
note at the top). The BOOT button is still interrupt-driven rather than polled
(`button_init()`), which is simply the better design; keep it that way.

**Occupancy reporting depends on a local patch in `managed_components/`**, which
is gitignored and does not survive a clean checkout. `attribute::update()` does
*not* work for the Occupancy attribute — reads are served by the registered
`OccupancySensingCluster`, so it must be set via the cluster's own
`SetOccupancy()`, reached through a patched-in accessor. Symptom if the patch is
missing: writes return `ESP_OK` but every read returns 0 and Home Assistant
never updates. Full explanation and the open decisions are in
[README.md](README.md) — read that before touching `matter_report_occupancy()`
or bumping `esp_matter`.

**All three `StartUp*` attributes must stay null.** esp_matter defaults every
one of them to a concrete value, and the Matter spec treats a non-null value as
"force this on power up", which silently discards the persisted light state:

| Attribute | esp_matter default | What it forced on boot |
|---|---|---|
| `StartUpOnOff` | `0` | light comes up Off |
| `StartUpCurrentLevel` | `0` (clamps to `min_level` 1) | minimum brightness |
| `StartUpColorTemperatureMireds` | `0x00fa` (250) | `ColorMode` → colour temperature, discarding the restored XY colour |

Null means "restore previous value" in each case. They are set in
`create_endpoints()`, but note that config only applies to a **fresh NVS** — a
device flashed with an earlier build still has the old value stored, so
`matter_setup()` also writes the null back explicitly at runtime.

Light state is restored from the cluster's own non-volatile attributes
(On/Off, CurrentLevel, CurrentX/Y, ColorTemperatureMireds and **ColorMode**) —
don't add a parallel NVS store for any of it. ColorMode is maintained by the
Matter colour-control server and tells the restore path whether the light was
last driven by the colour wheel or the temperature slider.

The colour fade (`status_led_fade_rgb()`) runs in a task that exits as soon as
it reaches the target, rather than a permanently resident animation task.
