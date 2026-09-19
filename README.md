# esp-demo-matter

Minimal Matter test device for the Waveshare ESP32-C6-LCD-1.3 (display unused),
built on native ESP-IDF v6.1 + `espressif/esp_matter`.

One endpoint over Matter-over-Thread:

- **Extended Color Light** — onboard WS2812 RGB LED (GPIO8), also switchable
  with the BOOT button (GPIO9).

See [CLAUDE.md](CLAUDE.md) for architecture, platform quirks and build commands,
and [CHANGELOG.md](CHANGELOG.md) for user-facing history.

## Build

```bash
source ~/.espressif/v6.1/esp-idf/export.sh
idf.py build
idf.py flash monitor
```
