# Changelog

Entries are grouped by date, newest first.

---

## 2026-08-16

### Added
- Initial version of this demo device: a minimal Matter test firmware for the Waveshare ESP32-C6-LCD-1.3, built from the same ESP-IDF/Matter-over-Thread stack as the plant monitor project but with the screen left unused. Home Assistant sees two things — a full-color light (set any color from a color wheel, driving the onboard RGB LED) and a switch that flips every time the BOOT button is pressed. Since there's no screen, the pairing code needed to add the device in Home Assistant is printed to the serial console on first boot instead.
