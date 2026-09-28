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

### Releasing an update (local script)

`tools/ota/matter_ota.py` does the whole loop from this machine: bump the
version, build, and offer the new image to every paired light. The version
bump and build run on the host (ESP-IDF). The Matter parts run in a Docker
container that is built on first use (`tools/ota/Dockerfile`), using Home
Assistant's prebuilt CHIP controller wheels and
[`chip-ota-provider-app`](https://github.com/home-assistant-libs/matter-linux-ota-provider).

The script uses **its own fabric** alongside Home Assistant (Matter
multi-admin), so HA keeps controlling the light as before. The fabric's keys
live in `~/.local/state/esp-demo-matter-ota/`. Keep that directory: deleting it
means pairing every light again.

One-time, per light:

1. In Home Assistant open the light → ⋮ → *Share device*, and copy the
   pairing code.
2. `tools/ota/matter_ota.py pair <code>` (it assigns node 1, 2, … automatically).

Each release:

```bash
tools/ota/matter_ota.py release     # bump patch + version number, build, offer, wait
tools/ota/matter_ota.py status      # version and update state per light
tools/ota/matter_ota.py offer       # re-offer the current build (no bump)
```

`release` starts the provider, tells each light where it is
(`AnnounceOTAProvider`), and waits until every light reports the new version.
Lights already on that version are skipped. Downloading ~1.7 MB over Thread
takes a few minutes. The provider's log is at
`~/.local/state/esp-demo-matter-ota/provider.log`.

On the device's serial console an update shows up as `OTA: download in
progress` → `download complete` → `apply complete — rebooting`, then
`First boot of updated firmware — marking it valid` after the reboot.

Requirements:

- **Docker.**
- **A route to the Thread network.** The border router advertises the Thread
  prefix on the LAN (`ip -6 route` shows `fd..::/64 via fe80::… proto ra`),
  so pinging a light's `fd..` address from this machine should work. The
  addresses are listed by `ot-ctl srp server host` on the border router.
- **An inbound firewall rule for the provider.** During an update the *light*
  opens the connection to the provider on this machine (UDP 5565, set by
  `PROVIDER_PORT`), so a default-deny firewall drops it and the download never
  starts. If ufw is active, `release` and `offer` check for this rule and add
  it (via `sudo`) for each Thread prefix they find in `ip -6 route`. The
  manual equivalent is:

  ```bash
  sudo ufw allow proto udp from fd0a:7d78:af03:1::/64 to any port 5565 comment 'Matter OTA provider'
  ```

  Use your own Thread prefix (from `ip -6 route`). mDNS (UDP 5353 multicast)
  is already allowed by ufw's default rules.
