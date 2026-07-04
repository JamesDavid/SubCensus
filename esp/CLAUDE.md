# esp/CLAUDE.md — SubCensusEsp notes

Headless ESP32 + CC1101 node. Same **CC1101** as the Zero, so it inherits the Zero's capture
model and produces **identical feature vectors** (System §7). No display — review/label/config
happen in a browser over WiFi. `SubCensus_System.md` is authoritative.

## Invariants (inherit repo `../CLAUDE.md`)
- **System.md is authoritative.** Do NOT modify the shared contract (`shared/taxonomy.yaml`,
  `shared/schema/`, `shared/core/`, the signatures/place/catalog definitions). Work in `esp/`.
- **shared/core compiles into the firmware** via `src/subcensus_core.c` (unity build) — the
  SAME feature vector / cadence / k-NN / CRC / differential as the Zero (do not reimplement).
- **RF boundary:** fixtures prove *processing*; a real CC1101 proves *physics*. Only live
  RSSI / capture / TX need hardware — mark `TODO(hw)`.
- **Monitoring is passive.** Replay/edit-TX is optional (CC1101 can TX), off by default,
  explicit + TX-allow-list gated (Esp §3, System §7b).

## Build (PlatformIO)
```
pip install platformio
cd esp
python -m platformio run -e esp32dev      # compile-check (no device)
python -m platformio run -e esp32dev -t upload   # flash over USB (or OTA once WiFi is up)
python -m platformio device monitor       # ESP_LOG / Serial
```
Platform pinned to `espressif32 @ 6.5.0` (Arduino core 2.0.x) for a reliable framework fetch.
`shared/core` is exposed via `build_flags = -I../shared/core` and unity-built in
`src/subcensus_core.c` (fbt/PlatformIO can't cleanly reference `../` sources otherwise).

## Generated headers
`src/census_taxonomy.h` + `src/census_schema.h` are **generated** from `shared/` (System §10),
alongside the Zero's. Regenerate: `python -m subcensus_tools.codegen` (from `tools/`).

## Hardware-free test strategy (Debug §3/§6)
An ESP32 firmware can't run headlessly here (no WiFi emulator), so:
- **Native tests** (`esp/test/`, via `zig cc`) cover `shared/core` + the ESP's
  hardware-independent logic (`src/esp_*.c`: settings, place model, census_log, rotation,
  classification glue). This is the bulk of correctness — no device.
- **Web UI** is tested against a host **mock node** (`tools`/Python) serving the SAME static
  assets + JSON/WebSocket contract, driven by httpx/Playwright; a Python HTTP/WS **driver**
  targets a real node once flashed.
- **`pio run`** is the per-milestone compile-check. Live radio/WiFi/flash = `TODO(hw)`.

## Pin plan (Esp §2)
CC1101 on VSPI: SCK 18, MOSI 23, MISO 19, CS 5; **GDO0 34** (input-only → RMT edge capture),
GDO2 35; optional SD CS 13. 3.3 V logic — no level shifter. Capture (RMT+CC1101) pinned to one
core, WiFi/web to the other (Esp §3 concurrency).

## Layout
```
platformio.ini      esp32dev (Arduino) env; -I../shared/core; unity core
src/
  main.cpp          firmware entry (WiFi/NTP/web/CC1101/capture) — grows per milestone
  subcensus_core.c  unity build of shared/core
  esp_settings.{c,h}  pure-C settings model + serialize (NVS/LittleFS) — host-tested
  esp_place.{c,h}     pure-C place_id slug + path helpers — host-tested
  census_taxonomy.h   GENERATED     census_schema.h  GENERATED
data/               LittleFS web assets (static UI)
test/               native (zig cc) unit tests for the esp_* logic + shared/core
```

## Status
M1 in progress (skeleton). See `../docs/SubCensusEsp_Spec.md` §8 for the milestone list.
