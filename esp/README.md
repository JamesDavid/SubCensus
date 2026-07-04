# SubCensusEsp — ESP32 + CC1101 node

The cheap, always-on, **networked** narrowband node. Same **CC1101** as the Zero, so it
inherits the Zero's capture model and produces **identical feature vectors** (System §7) — the
cleanest fingerprint parity of the three sensors. Headless: review/label/config happen in a
browser over WiFi. Spec: [`../docs/SubCensusEsp_Spec.md`](../docs/SubCensusEsp_Spec.md); shared
contract: [`../docs/SubCensus_System.md`](../docs/SubCensus_System.md). See [`CLAUDE.md`](./CLAUDE.md).

## Flash it from your browser

No toolchain needed — a one-click **web flasher** (ESP Web Tools / Web Serial) lives in
[`flasher/`](./flasher/) and deploys to GitHub Pages via CI. Once Pages is enabled
(Settings → Pages → Source: GitHub Actions), open **https://jamesdavid.github.io/SubCensus/**
in Chrome/Edge, wire the CC1101 (diagram + pin map on the page), and click Install. See
[`flasher/README.md`](./flasher/README.md).

## Build (PlatformIO)

Platform pinned to `espressif32 @ 6.5.0` (Arduino core 2.0.x) for a reliable framework fetch.

```
pip install platformio
cd esp
python -m platformio run -e esp32dev            # compile-check (no device)
python -m platformio run -e esp32dev -t buildfs # pack the LittleFS data/ web UI
python -m platformio run -e esp32dev -t upload  # flash firmware over USB (or OTA once on WiFi)
python -m platformio run -e esp32dev -t uploadfs# flash the LittleFS image (web UI)
python -m platformio device monitor             # ESP_LOG / Serial
```

`shared/core` is compiled into the firmware via `src/subcensus_core.c` (unity build) and
exposed with `build_flags = -I../shared/core` — CONFIRMED linking. The core is float-only
(suits the ESP32 FPU).

## Hardware (Esp §2)

CC1101 on the VSPI bus: **SCK 18, MOSI 23, MISO 19, CS 5**; **GDO0 34** (input-only → RMT edge
capture), GDO2 35; optional SD CS 13. 3.3 V logic — no level shifter. Capture (RMT + CC1101) is
pinned to one core, WiFi/web to the other.

## Web UI + API (headless — the UI replaces the screen)

Full single-page UI served from LittleFS `data/index.html`: **Live feed** (WebSocket),
**Review/label** (candidates + confirm-appends-fingerprint), **Bands** (occupancy/watchlist +
recon), **Places**, **Settings**. JSON API: `/api/status`, `/api/captures`, `/api/candidates`,
`/api/label`, `/api/occupancy`, `/api/watchlist`, `/api/recon`, `/api/sweep`, `/api/camp`,
`/api/settings`, `/api/places`, `/api/place`, `/api/brain/sync`, and the fixture-inject
`/api/debug/inject` (drives the full decode→classify→log→WS path with no live RF, Esp §3.4).

## Storage (Esp §4)

LittleFS default (`signatures/` global brain, per-place `census_log.csv`/`occupancy.csv`/
`watchlist.csv`, capped/rotating RAW captures). Same on-disk schema as the Zero/Pi (System §4/§9).
Optional SD (full per-place folder model) is a later, *optional* milestone.

## Tests (no hardware — an ESP32 can't run headlessly here)

```
# shared/core + ESP hardware-independent logic (settings, place, capture-decode, census_log,
# rotation, occupancy CSV, fingerprints, mqtt) — native, via zig cc
python esp/test/run_tests.py                # 8 test files

# web-UI driver contract (status/captures/candidates/settings/places/WS against the served shape)
python -m pytest esp/tools/                 # 7 tests
```

`esp/tools/esp_web.py` also has a `NodeClient` (httpx/WebSocket) to drive a **real** node
(USB/OTA) and fixture-inject over `/api/debug/inject`. Live radio (RMT capture, CC1101 RSSI/TX),
live WiFi/MQTT/OTA, and browser rendering are on-device steps (`TODO(hw)`).

## Status

Non-optional milestones **complete**:
- **M1** skeleton (WiFi/NTP/web/CC1101/settings/place) · **M2** RMT capture + Camp + census_log +
  rotation + WebSocket feed · **M3** Recon+Sweep → occupancy/watchlist · **M4** classification
  (feature vector + gated k-NN) + confirm-appends-fingerprint · **M5** web UI complete ·
  **M7** MQTT/HA discovery + brain sync + OTA.

*Optional* remaining (spec §8): **M6** SD auto-detect (full per-place folder model; LittleFS is
the working fallback) · **M8** replay + edit-before-transmit + field-map over the web UI
(CC1101 TX; TX-allow-list gated, single-frame, off by default).
