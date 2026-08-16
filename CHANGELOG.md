# Changelog

Entries are grouped by date, newest first.

---

## 2026-08-16

### Added
- Initial version of this demo device: a minimal Matter test firmware for the Waveshare ESP32-C6-LCD-1.3, built from the same ESP-IDF/Matter-over-Thread stack as the plant monitor project but with the screen left unused. Home Assistant sees two things — a full-color light (set any color from a color wheel, driving the onboard RGB LED) and a switch that flips every time the BOOT button is pressed. Since there's no screen, the pairing code needed to add the device in Home Assistant is printed to the serial console on first boot instead.

### Changed
- Removed the separate switch shown in Home Assistant. The BOOT button now directly turns the RGB light on/off instead — press it and the light's own on/off state flips, the same state Home Assistant's light card controls. One light, controllable from either the button or Home Assistant, instead of two disconnected entities.

### Fixed
- Pressing the BOOT button no longer flashes the RGB LED. That green blink was meant as a quick "got it" confirmation for the button press, but since the same LED also shows whatever color Home Assistant has set for the light, it was briefly overwriting that color on every press. The switch still toggles correctly; there's just no LED flash tied to it now.
- Turning the light on/off from Home Assistant now actually switches the LED. Before the first color was ever set, the firmware assumed the light's color was black, so switching it "on" just left the LED dark — it now assumes the same default warm-white color Matter itself starts the light at, matching what a controller would expect to see.
