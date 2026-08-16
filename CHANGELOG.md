# Changelog

Entries are grouped by date, newest first.

---

## 2026-08-16 (2)

### Added
- Colour changes now fade smoothly into place over about half a second instead of snapping to the new colour. Dragging the colour wheel in Home Assistant follows your finger rather than queueing up a backlog of jumps.

### Fixed
- The light now comes back exactly as you left it after a power cut — same on/off state, same brightness, same colour. Previously it always powered up switched off and at minimum brightness, discarding whatever was set before, because the firmware asked Matter for the wrong start-up behaviour.
- The colour-temperature tab in Home Assistant now works. It was showing a strange, mostly-flat gradient and moving the slider did nothing, because the firmware ignored temperature entirely and advertised an unrealistic range. It now renders warm-to-cool white properly across a normal lamp's range, and the light remembers whether you last used the colour wheel or the temperature slider.
- Setting a colour, cutting the power and switching back on no longer brings the light up as bright white. The firmware was asking Matter to force a fixed colour temperature at every power-up, which overrode the colour that had just been restored.

### Changed
- The light now stays awake and responds immediately instead of sleeping between checks. It was configured as a battery-style sleepy device inherited from the plant monitor project, which meant it could take up to 20 seconds to notice a command from Home Assistant. Being mains-powered, it now stays on the Thread network full time and even helps relay traffic for other devices.
- Renamed the device from "Matter Demo (C6)" to "Matter Light Demo" (shown in Home Assistant and printed on the hardware version string).

## 2026-08-16

### Added
- Initial version of this demo device: a minimal Matter test firmware for the Waveshare ESP32-C6-LCD-1.3, built from the same ESP-IDF/Matter-over-Thread stack as the plant monitor project but with the screen left unused. Home Assistant sees two things — a full-color light (set any color from a color wheel, driving the onboard RGB LED) and a switch that flips every time the BOOT button is pressed. Since there's no screen, the pairing code needed to add the device in Home Assistant is printed to the serial console on first boot instead.

### Changed
- Removed the separate switch shown in Home Assistant. The BOOT button now directly turns the RGB light on/off instead — press it and the light's own on/off state flips, the same state Home Assistant's light card controls. One light, controllable from either the button or Home Assistant, instead of two disconnected entities.

### Fixed
- Pressing the BOOT button no longer flashes the RGB LED. That green blink was meant as a quick "got it" confirmation for the button press, but since the same LED also shows whatever color Home Assistant has set for the light, it was briefly overwriting that color on every press. The switch still toggles correctly; there's just no LED flash tied to it now.
- Turning the light on/off from Home Assistant now actually switches the LED. Before the first color was ever set, the firmware assumed the light's color was black, so switching it "on" just left the LED dark — it now assumes the same default warm-white color Matter itself starts the light at, matching what a controller would expect to see.
