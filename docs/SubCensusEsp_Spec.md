# SubCensusEsp — Headless ESP32 + CC1101 Node

**Target:** ESP32-WROOM-32 30-pin devkit + CC1101 sub-GHz module. Firmware in PlatformIO (Arduino or ESP-IDF). Headless — serves its UI over WiFi.

> **Shared contract lives in `SubCensus_System.md`** — Places, the signature DB / classification brain, the feature-vector schema, the label taxonomy, the host-side tools, and the shared `occupancy.csv`/`watchlist.csv`/catalog-record schemas are defined there and are authoritative. This spec covers only ESP-specific hardware, capture, storage, web UI, and build; overlapping content defers to the System doc on any conflict.

---

## 1. What it is
The cheap, always-on, **networked** narrowband node. Same **CC1101** as SubCensusZero, so it inherits the Zero's capture model and produces **identical feature vectors** (the cleanest fingerprint parity of the three sensors). Unlike SubCensusPi it is narrowband (CC1101, not a wideband RTL-SDR) and it's from-scratch ESP32 firmware, not a Pi service. No display: review/label/config all happen in a browser.

Scope for v1: **single node, no mesh.** (A future hub-reporting mode is out of scope here.)

---

## 2. Hardware & pin plan
CC1101 on the ESP32 **VSPI hardware bus** — no bit-bang, no compromises:

| Signal | GPIO | Notes |
|---|---|---|
| SCK | 18 | VSPI |
| MOSI | 23 | VSPI |
| MISO | 19 | VSPI |
| CC1101 CS | 5 | VSPI CS |
| **GDO0** | 34 | input-only → ideal for RMT edge-capture (§3) |
| GDO2 | 35 | optional, input-only |
| SD CS (optional) | 13 | SD module shares VSPI (18/23/19), own CS (§4) |

- Avoid strapping pins (0, 2, 12, 15) for anything critical — the plan above steers clear.
- CC1101 is 3.3V logic → wire straight to ESP32, **no level shifter**. Power the module from 3V3.
- Antenna tuned to the band(s) of interest (whip/spring for ~433 or ~915).
- Bus sharing: CC1101 config/FIFO and the optional SD are both infrequent relative to capture (which rides GDO0/RMT, off-bus), so separate CS lines on one VSPI bus is fine.

---

## 3. Capture model (inherits SubCensusZero)
Same CC1101, so this node runs the Zero's narrowband pipeline verbatim in concept — see **SubCensusZero_Spec.md §3 (Recon → Sweep/Camp)** and **§5 (capture engine)**; the shared feature vector is **System §7**. ESP-specific realization:

- **Edge capture via the RMT peripheral.** Timestamp GDO0 transitions in hardware for RAW OOK/FSK timing — clean, ISR-light, and independent of WiFi/web activity. This is the ESP analog of the Flipper's async RAW capture.
- **RSSI + config** over hardware VSPI (a CC1101 driver such as ELECHOUSE_CC1101, or a thin direct SPI driver).
- **Modes:** Recon (stepped-RSSI occupancy sweep → `occupancy.csv` + `watchlist.csv`), Sweep, Camp — identical semantics to the Zero, including the **no-valid-recon** fallback (System §9): monitoring is never blocked on recon — Sweep falls back to the preset list with a dismissable hint, Camp always runs on an explicit frequency (only the busiest-watchlist auto-pick is disabled without a watchlist).
- **Feature vector** computed from the RMT-captured pulse stream, binned/normalized exactly per System §7 so vectors match Zero- and Pi-derived ones.
- **Monitoring is passive** (never transmits while surveying). **Replay-to-identify** is optional (the CC1101 can TX) and, if built, follows the Zero's stance: explicit/manual only, TX-allow-list gated. Off by default.
- **Concurrency:** pin the capture path (RMT + CC1101) to one core and WiFi/web to the other (FreeRTOS tasks) so the web server never disturbs capture timing.
- **Cadence (System §7a):** being always-on makes this a **good cadence measurer** for whatever band it camps on — over hours/days it accumulates solid inter-arrival statistics. Keep a **compact per-device running estimator** (last_ts, count, running mean/variance, small interval histogram for harmonic folding) in RAM/LittleFS rather than a full event log, and write the derived `cadence_*` fields to the catalog record.

**Honest limitation vs the Zero:** the Flipper ships a rich SubGhz *decoder* registry; a bare ESP32 does not. This node is a strong **capture + fingerprint** sensor — it classifies via the gated k-NN against `fingerprints.csv` (System §6) and can decode a limited set of common OOK protocols (e.g. via rc-switch-style logic) — but full protocol decoding is deferred to the host-side tools. Undecoded captures still get feature-vectored and matched; that's the point of the fingerprint brain.

---

## 4. Storage (tiered: LittleFS default, SD if present)
No SD is assumed on a bare devkit. Detect hardware at boot and pick a tier:

- **LittleFS (default, internal flash):** holds `signatures/` (the global brain, synced over WiFi — §6), the active place's `census_log.csv`, `occupancy.csv`, `watchlist.csv`, and a **capped, rotating** store of RAW capture files (flash is small — cap by count/size and rotate oldest; keep metadata rows even after a RAW file is rotated out).
- **Optional microSD (auto-detected on VSPI at boot):** if a card is present, use the **full per-place folder model** exactly like Zero/Pi (System §4) — `places/<place_id>/{occupancy.csv,watchlist.csv,census_log.csv,captures/}` + global `signatures/` — with no capture-rotation limit beyond card size.
- Same on-disk schema either way (System §4/§9); only capacity and rotation policy differ. A place folder from this node is a valid input to the host-side `export_place`/`analyze_place` tools (System §8).

---

## 5. Web UI (headless — this replaces the screen)
`ESPAsyncWebServer` + a **WebSocket** live feed. Serves, from the node itself:
- **Live feed:** captures as they land (freq, RSSI, best match + confidence).
- **Review / label:** browse `census_log`, see top-N classification candidates (System §6), accept one or pick from the taxonomy (System §5); confirming a label appends the feature vector to the global brain (System §6).
- **Occupancy / watchlist:** review the Recon activity map (ranked hot bins) and the active watchlist; per-entry **pin / exclude / camp here**. Recon controls: **Run (Accumulate default / Fresh)** and **Reset** (confirm; keep-or-wipe pins) — cumulative per place, per System §9.
- **Place management:** create/switch/rename/delete places (System §4).
- **Settings:** mode (Recon/Sweep/Camp), frequency list, thresholds, capture preset(s), WiFi/MQTT config.
- **Field-map discovery / edit (System §7b):** browser equivalent of the Zero's editor. Shows the passive differential-analysis overlay on an unknown (static / slow-varying / counter / checksum segments), lets the user label segments and propose a `field_maps/` entry, and — because the node has a CC1101 — supports **active confirmation** and **replay/edit-before-transmit**: transmit a captured or edited frame to your *own* device from the browser and watch it react. Same guards as the Zero (own-device framing, **TX-allow-list gated**, single-frame only — no auto-increment/sweep, edited TX logged distinctly, passive-while-scanning unchanged). Heavy corpus crunching (differential + CRC search) can defer to the host tools; the node handles labeling + confirmation.

Mirrors the Pi dashboard's concepts (plus TX, which the Pi lacks); all schema/semantics defer to System. Keep the front-end static assets in LittleFS.

---

## 6. Networking
- **WiFi** provisioning: config portal on first boot (fallback SoftAP) or committed credentials; store in NVS/LittleFS.
- **NTP** for real wall-clock timestamps on captures/log.
- **MQTT → Home Assistant** auto-discovery (like SubCensusPi §MQTT) so identified devices surface as HA entities — fits the existing HA/MQTT setup.
- **Brain sync:** pull the global `signatures/` (`protocol_map.csv` + `fingerprints.csv`) from a host/share over WiFi and push this node's user-confirmed fingerprints back, so it participates in the shared brain without an SD card. Simple HTTP GET/PUT or MQTT payloads; host-side `build_signatures.py` remains the merge point (System §8).
- **OTA** firmware updates (ArduinoOTA / HTTP OTA).

---

## 7. Build
- PlatformIO: `board = esp32dev`, `framework = arduino` (ESP-IDF acceptable if preferred for RMT control).
- Libraries: a CC1101 driver (ELECHOUSE_CC1101 or a thin direct SPI driver), `ESPAsyncWebServer` + `AsyncTCP`, `ArduinoJson`, `LittleFS`, `SD`/`SD_MMC` (optional path), an MQTT client (`PubSubClient`/`AsyncMqttClient`), `ArduinoOTA`. RMT via the core SDK.
- Partition scheme with enough LittleFS for static web assets + rotating captures; enable OTA partitions.

---

## 8. Milestones (single node, no mesh)
1. Skeleton firmware: WiFi + NTP + async web server (serves a static page) + CC1101 init over VSPI + settings in NVS/LittleFS + place model (default place).
2. Capture engine: RMT GDO0 edge capture → RAW; CC1101 RSSI; **Camp** mode → LittleFS `census_log.csv` + capped/rotating capture files + WebSocket live feed.
3. Recon + Sweep (inherit Zero pipeline): emit `occupancy.csv` + `watchlist.csv`; Sweep/Camp consume the watchlist.
4. Classification: feature vector (System §7) + gated k-NN against `fingerprints.csv` (System §6); surface candidates in the web Review UI; confirm-appends-fingerprint.
5. Web UI complete: review/label, occupancy/watchlist, place management, settings.
6. Optional SD auto-detect → full per-place folder model (System §4); LittleFS remains the fallback with rotation.
7. MQTT/HA discovery + brain sync over WiFi + OTA.
8. (Optional) Replay + edit-before-transmit + field-map discovery over the web UI (§5, System §7b): differential overlay + segment labeling on unknowns, active own-device confirmation via CC1101 TX, TX-allow-list gated, single-frame only, edited-TX logged distinctly.
