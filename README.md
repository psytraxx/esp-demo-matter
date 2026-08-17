# esp-demo-matter

Minimal Matter test device for the Waveshare ESP32-C6-LCD-1.3 (display unused),
built on native ESP-IDF v5.5.5 + `espressif/esp_matter`.

Two endpoints over Matter-over-Thread:

- **Extended Color Light** — onboard WS2812 RGB LED (GPIO8), also switchable
  with the BOOT button (GPIO9).
- **Occupancy Sensor** — HLK-LD2410 24 GHz presence radar over UART
  (IO2/IO3). See [docs/ld2410_wiring.md](docs/ld2410_wiring.md).

See [CLAUDE.md](CLAUDE.md) for architecture, platform quirks and build commands,
and [CHANGELOG.md](CHANGELOG.md) for user-facing history.

## Build

```bash
source ~/.espressif/v5.5.5/esp-idf/export.sh
idf.py build
idf.py flash monitor
```

---

## TODO — review the local esp_matter patch for occupancy reporting

**This build depends on a hand-patched file inside `managed_components/`, which
is gitignored and will be lost on a clean checkout or component re-fetch.**
Without it the occupancy endpoint silently stops reporting: the value appears to
be written (`attribute::update()` returns `ESP_OK`) but every read — including a
controller's live read of `2/1030/0` — returns `0`, and Home Assistant's
occupancy `binary_sensor` never changes state.

### Why the patch is needed

Reads for a *registered* cluster are served by the cluster object, not by
esp_matter's own attribute store
(`esp_matter_data_model_provider.cpp`, `ReadAttribute`):

```cpp
if (auto *cluster = mRegistry.Get(request.path); cluster != nullptr) {
    return cluster->ReadAttribute(request, encoder);   // registered cluster wins
}
// only an unregistered path falls through to attribute::get_val_internal()
```

`OccupancySensing` is registered, so `Occupancy` lives in
`OccupancySensingCluster::mOccupancy`. `attribute::update()` writes a parallel
store that is never consulted for this cluster, so application writes no-op
while still reporting success. The correct setter is the cluster's own
`SetOccupancy()`, which also raises the report via `NotifyAttributeChanged()`.

Upstream acknowledges the gap in `set_val()`:

```
// TODO: If not writable, we could use the cluster-specific setter API to update the value
```

but keeps the cluster instance in a file-local `gServers` map with no accessor.
CHIP's own `FindClusterOnEndpoint()` does not link here, because esp_matter uses
its own data-model provider instead of CHIP's codegen integration.

Checked on `release/v1.5` (in use), `release/v1.6` and `main` — all three have
the same private structure, and 1.5.1 is the newest on the component registry,
so upgrading does not fix it.

Note: Espressif's own `examples/sensors` uses `attribute::update()` for
occupancy and would hit this same problem — it is not a reliable reference here.

**Confirmed independently upstream:**
[espressif/esp-matter#1738](https://github.com/espressif/esp-matter/issues/1738)
(open) traces the identical bug through the identical call chain and reaches
the same workaround shape — call the registered cluster's own setter. Their
writeup references a `#include <clusters/occupancy_sensing/integration.h>`
exposing `FindClusterOnEndpoint()`, which does not exist in the 1.5.1 tree
vendored here (possibly a newer/different esp-matter revision); the accessor
below was verified working on what we actually have. Worth checking this issue
when bumping `esp_matter` — if it's fixed, the patch step in `CMakeLists.txt`
and `patches/esp_matter_occupancy_accessor.cpp.in` can be deleted.

### The patch

Appended to
`managed_components/espressif__esp_matter/components/esp_matter/data_model_provider/clusters/occupancy_sensing/integration.cpp`:

```cpp
chip::app::Clusters::OccupancySensingCluster *
esp_matter_get_occupancy_cluster(chip::EndpointId endpointId)
{
    auto it = gServers.find(endpointId);
    if (it == gServers.end() || !it->second.IsConstructed()) {
        return nullptr;
    }
    return &it->second.Cluster();
}
```

`matter_report_occupancy()` in `main/matter_setup.cpp` forward-declares it and
calls `SetOccupancy()` on the Matter thread.

### What to decide

1. ~~**Make it survive a clean build**~~ — done: `CMakeLists.txt` appends
   `patches/esp_matter_occupancy_accessor.cpp.in` at configure time and fails
   the build loudly if the accessor doesn't land, so this is no longer a silent
   trap on a fresh checkout.
2. **Raise it upstream** — already tracked at
   [espressif/esp-matter#1738](https://github.com/espressif/esp-matter/issues/1738)
   (open, filed by someone else who hit the same bug independently). No PR from
   this project yet; consider filing one if the issue stalls.
3. **Re-check on each esp_matter bump** — if upstream exposes an official
   setter (in #1738 or otherwise), delete the patch step, the `.cpp.in` file,
   and this section.

The same private-`gServers` pattern applies to other code-driven clusters
(e.g. `boolean_state`), so anything similar added later will hit this too.
