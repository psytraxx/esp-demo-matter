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

## OTA updates (Matter)

The device is a Matter **OTA Requestor**: endpoint 0 has the OTA Software
Update Requestor cluster, and when a controller announces an update the device
downloads it over Thread (BDX) into the other app slot, checks it, and reboots
into it. If the new firmware crashes before the Matter stack comes up, the
bootloader rolls back to the previous version.

**One-time migration:** the partition table changed from a single factory app
to two OTA slots, so the first flash after this change must be over USB with
`idf.py flash` (this also writes the new partition table). Commissioning is
kept, because both NVS partitions stay where they were.

### Releasing an update

1. Bump `PROJECT_VER` **and** `PROJECT_VER_NUMBER` in the root `CMakeLists.txt`.
   The provider only offers an image whose number is higher than the running one.
2. `idf.py build` — this writes `build/esp_demo_matter-ota.bin`, the app wrapped
   in the Matter OTA header (VID `0xFFF2`, PID `0x8003`, version). Inspect it with
   `python managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/app/ota_image_tool.py show build/esp_demo_matter-ota.bin`.
3. Serve it from an OTA Provider on the same fabric:
   - **Home Assistant:** the Matter Server add-on is a provider. Test VIDs
     (0xFFF1–0xFFF4) are not in the DCL, so point the add-on at a local
     update file (its "OTA provider" directory with a JSON entry for this
     VID/PID) and use the device's *Update* entity.
   - **chip-tool** (lab setup): run `chip-ota-provider-app -f build/esp_demo_matter-ota.bin`,
     commission it, grant it ACL access to the device, then
     `chip-tool otasoftwareupdaterequestor announce-otaprovider <provider-node> 0 0 0 <device-node> 0`.
4. Watch the serial log: `OTA: download in progress` → `download complete` →
   `apply complete — rebooting`, then after reboot
   `First boot of updated firmware — marking it valid`.
