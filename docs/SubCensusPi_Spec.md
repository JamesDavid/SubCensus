# SubCensusPi — RTL-SDR / Raspberry Pi ISM Census

**Target:** Linux / Raspberry Pi, Python 3.11+, RTL-SDR (Blog v3 or v4).
**Author intent:** The stationary, run-it-for-a-week counterpart to the Flipper tool. Wideband, decodes hundreds of ISM device protocols via `rtl_433`, logs everything continuously, captures the undecodable for later classification, and surfaces it all in a small dashboard. Ties into the existing Home Assistant / MQTT setup.

> Working name: **SubCensusPi**. Part of one system with **SubCensusZero** (the Flipper tool).

> **Shared contract lives in `SubCensus_System.md`** — Places, the signature DB / classification brain, the feature-vector schema, the label taxonomy, the host-side tools, the shared `occupancy.csv`/`watchlist.csv` artifacts + catalog record, and the repo layout are defined there and are authoritative. This spec covers only Pi-specific capture, decode, dashboard, and deployment; overlapping sections below reference the System doc and defer to it on any conflict.

---

## 1a. Relationship to SubCensusZero
See **System §3** for the full unified-vs-platform-native boundary. In short: SubCensusPi shares the whole data/intelligence layer (Places, brain, feature vectors, taxonomy, artifacts, host tools) and keeps only its capture model, web UI, RX-only nature, and `rtl_433`-native modulation handling platform-specific.

---

## 1. Goals & non-goals
**Goals**
- Continuously receive and decode ISM devices across the common bands.
- Persist every reception; roll receptions up into **device** rows (one physical device = one nameable entry) with first/last seen, count, typical freq, avg SNR.
- Capture **undecoded** signals (IQ + pulse data) so unknowns can be classified later.
- Web dashboard to name/label/room-tag devices and watch live activity.
- Optional MQTT → Home Assistant auto-discovery so found devices appear as HA entities.

**Non-goals**
- Not a security/replay tool. Receive, decode, catalog only.
- Not a replacement for the Flipper walk-around; complementary (stationary long-run vs portable localization).

---

## 2. Architecture
```
 RTL-SDR ──> rtl_433 (-F json)  ──stdout──>  collector.py
                                              │  (parse, dedup, enrich)
                                              ├─> SQLite (devices, events, unknowns)
                                              ├─> MQTT (optional) ─> Home Assistant
 rtl_433 (-S / -A) ─> IQ/pulse files ─> unknowns table + offline analysis
                                              │
 web (FastAPI) <── reads SQLite ──> dashboard: live feed, device list, labeling
```
Two long-running services under systemd: **collector** (owns rtl_433) and **web**. IQ/pulse capture can be a mode of the same rtl_433 invocation or a second scheduled pass.

---

## 3. Frequency strategy (support both)
- **Single dongle, hopping:** `rtl_433 -f 433.92M -f 915M -f 315M -H 30` (hop every 30 s). Simple; misses events on bands it isn't currently on (same tradeoff as the Flipper sweep — document it).
- **Multi-dongle, parallel:** one `rtl_433` per dongle pinned by serial (`-d :<serial>`), each locked to a band (e.g. 433.92, 915, 315). True simultaneous coverage. Collector merges all JSON streams (tag each event with source dongle/band).
- Config-driven (§8) so the same code runs either way.
- **Recon is optional here (System §9):** the Pi is wideband-continuous, so with no occupancy pass / watchlist it simply monitors its configured frequencies (hop or multi-dongle) unprioritized. The watchlist only *reorders/assigns* attention; it is never a gate — monitoring runs regardless.
- Optional occupancy pass: `rtl_power` / `soapy_power` heatmap sweep over ~300–930 MHz to *see* where energy is even when `rtl_433` can't decode it — feeds a "busy but undecoded" view. Nice for the "what do I even have" question. **Emit per-place `occupancy.csv` + `watchlist.csv` in the SubCensusZero schema** (§9a) so the artifacts, cross-place compare, and LLM analysis are tool-agnostic. This is SubCensusPi's analog of the Zero's Recon Stage A — different engine (wideband SDR power sweep vs stepped RSSI), same outputs.

---

## 4. rtl_433 invocation
Baseline decode stream:
```
rtl_433 -F json -M time:iso:tz -M level -M protocol -M stats \
        -f 433.92M -f 915M -f 315M -H 30 -Y autolevel
```
- `-M level` gives rssi/snr/noise per event; `-M protocol` includes numeric protocol id; `-M stats` for periodic health.
- Prefer building rtl_433 from source for newest device decoders; note **RTL-SDR Blog v4 requires a current librtlsdr** (rtl_433 will fail to open a v4 on an old lib).
- Set `-p <ppm>` and `-g <gain>` per dongle in config.

**Unknown / undecoded capture (classify later):**
- `-A` pulse analyzer for on-the-fly pulse timing of unknown bursts.
- `-S unknown` (or `-S all`) to save triggered `.cu8` IQ snippets. Store path + trigger metadata in the `unknowns` table; later run offline `rtl_433 -r file.cu8 -A` or feed to a pulse-timing feature extractor. Gate this behind a config flag + disk-space guard (IQ is big).

---

## 5. Data model (SQLite, WAL mode)
```sql
devices(
  device_id TEXT PRIMARY KEY,   -- stable hash of (model,id,channel)
  model TEXT, dev_id TEXT, channel TEXT,
  place TEXT,                   -- Place profile (§9a); unified with SubCensusZero Places
  first_seen TEXT, last_seen TEXT, count INTEGER,
  typical_freq_hz INTEGER, avg_snr REAL,
  label TEXT, room TEXT, device_class TEXT, notes TEXT
);
events(
  id INTEGER PRIMARY KEY,
  ts TEXT, device_id TEXT, place TEXT, freq_hz INTEGER,
  rssi REAL, snr REAL, source TEXT,      -- source = dongle/band; place = location profile
  raw_json TEXT
);
unknowns(
  id INTEGER PRIMARY KEY,
  ts TEXT, place TEXT, freq_hz INTEGER, source TEXT,
  iq_path TEXT, pulse_summary TEXT,      -- pulse count, timing histogram
  label TEXT, device_class TEXT, notes TEXT
);
```
Collector logic: parse each JSON line → derive `device_id` from `(model,id,channel)` → upsert `devices` (update last_seen/count/avg_snr/typical_freq) → insert `events`, both stamped with the active `place`. Undecoded triggers → `unknowns`.

**Cadence (System §7a):** the Pi is the **strongest cadence measurer** — it hears devices continuously and timestamps every reception in `events`, so inter-arrival estimation is cleanest here. Compute the dropout-robust `cadence_*` fields from each device's `events` history and write them to the catalog record; the Pi is the preferred source when `build_signatures.py` reconciles cadence across tools.

---

## 6. Classification workflow
- **Auto:** `rtl_433` already yields model/type; grouping by `(model,id,channel)` means each physical device is one row to name.
- **Manual (dashboard, §7):** assign friendly `label` (“Garden temp sensor”), `room`, and `device_class` from the shared taxonomy (§10). Editing writes straight to SQLite.
- **Unknowns:** review queue showing pulse summary + a "play/inspect" link to the saved IQ; assign a class or discard. Optionally re-run saved IQ through newer rtl_433 later to see if it now decodes.

---

## 7. Web dashboard (FastAPI + SQLite)
Keep deps light for a Pi. Single-page, server-rendered (Jinja) + a little vanilla JS/HTMX for live updates; no heavy SPA.
- **Live feed:** last N receptions (time, model, freq, snr, room/label if known).
- **Devices:** table with count, last-seen, typical band, per-device activity sparkline; inline edit for label/room/class.
- **Unknowns:** review queue (§6).
- **Bands:** occupancy heatmap from the `rtl_power` pass + the derived watchlist; review ranked hot bins, per-entry **pin / exclude**. Recon controls: **Run (Accumulate default / Fresh)** and **Reset** (confirm; keep-or-wipe pins) — cumulative per place, per System §9. (RX-only: no camp-here TX action.)
- Read-only API endpoints return JSON for the same data (handy for scripting / jdbip-style writeups later).

---

## 8. Config (YAML)
```yaml
dongles:
  - serial: "00000001"
    freqs: [433.92M]
    gain: 40
    ppm: 1
  - serial: "00000002"
    freqs: [915M, 315M]
    hop_seconds: 30
capture_unknowns: true
place: home              # active Place profile (§9a); stamps all records
places_dir: /var/lib/subcensuspi/places   # per-place occupancy/watchlist/captures
signatures_dir: /var/lib/subcensuspi/signatures  # GLOBAL brain, shared with SubCensusZero
iq_dir: /var/lib/subcensuspi/iq
max_iq_gb: 20            # disk guard; stop capturing unknowns past this
db_path: /var/lib/subcensuspi/census.db
mqtt:
  enabled: true
  host: 127.0.0.1
  ha_discovery: true      # publish HA MQTT-discovery configs
web:
  host: 0.0.0.0
  port: 8080
```

## 9. Deployment
- **Install:** system `rtl-433` (or build from source for latest decoders) + `librtlsdr` (current, for v4); Python venv with `fastapi, uvicorn, jinja2, paho-mqtt, pyyaml`. Blacklist the `dvb_usb_rtl28xxu` kernel module so the SDR is free.
- **systemd:** `subcensuspi-collector.service` (spawns/pipes rtl_433, owns the DB writes) and `subcensuspi-web.service` (uvicorn). Restart=always. Collector should relaunch rtl_433 if it dies.
- **MQTT/HA:** either let `rtl_433` publish directly (`-F "mqtt://host,events=rtl_433/..."`) or have the collector republish with HA MQTT-discovery so devices show up as entities automatically. (Fits the existing Home Assistant / MQTT setup — discovered sensors can flow straight in.)
- **Hardware notes:** decent antenna per band (telescopic tuned ~433, separate for 915); USB extension to reduce Pi noise; watch dongle heat; set `ppm` from a known reference.

---

## 9a. Places (Pi specifics)
Place model, per-place vs global split, and layout: **System §4** (authoritative). Pi-specific realization:
- **Scoping:** `occupancy.csv` + `watchlist.csv` under `places_dir/<place>/` (§3), captured IQ under `places_dir/<place>/iq`, and a `place` column on the `devices`/`events`/`unknowns` SQLite rows.
- **Global (not per place):** the `signatures/` brain at `signatures_dir`.
- **Active place** from config (`place:`, default `home`), overridable per collector instance.
- The dashboard (§7) filters by place; the JSON API and the shared `export_place`/`analyze_place` tools take a `place` argument, so a Pi place and a Zero place are interchangeable inputs (System §8).

## 10. Label taxonomy
The `device_class` vocabulary is defined once in **System §5** (`shared/taxonomy.yaml`) and consumed by both tools. Do not maintain a separate list here.

## 10a. Signature database (Pi specifics)
The shared brain (`protocol_map.csv` + `fingerprints.csv`), its schema, the classification pipeline, and the `build_signatures.py` merge point are defined in **System §6–8**. Pi-specific points:
- **Feature extraction** runs on the saved IQ/pulse data (§4 unknowns), producing the canonical feature vector (System §7) — bin frequency and normalize timing exactly as System §7 requires so vectors match the Zero's RAW-derived ones.
- **Stronger seed:** `rtl_433`'s rich decode set makes the Pi the better *seed* source for `protocol_map` when `build_signatures.py` merges both tools' labeled data.
- **LLM analysis:** `export_place.py` / `analyze_place.py` (System §8) accept a Pi place by querying its SQLite into the same rolled-up bundle — same provider-agnostic, local-first analysis over RTL-SDR data.
- **Field-map discovery (System §7b):** the Pi is the **strongest at the passive layers** — its continuous corpus of every-reception rows in `events` gives the cleanest differential bitfield analysis + checksum search, and HA/MQTT values are co-located for ground-truth correlation ("these bits track temperature"). The dashboard (§7) shows the derived structure and lets the user label segments and confirm a proposed `field_maps/` entry. **RX-only → no active confirmation** (can't transmit an edited frame); that step is Zero/Esp only. Passive analysis + labeling is fully available here.


## 11. Milestones
1. Collector: rtl_433 JSON → SQLite (devices+events), single-dongle hop.
2. FastAPI dashboard: live feed + device list + inline labeling.
3. Multi-dongle parallel + source tagging.
4. Unknown capture (IQ/pulse) + review queue + disk guard.
5. MQTT / Home Assistant discovery.
6. Occupancy heatmap pass + read-only JSON API; **emit per-place `occupancy.csv`/`watchlist.csv` in the shared schema (§9a)**.
7. Shared signature DB export/import (§10a): emit `protocol_map`/`fingerprints`, ingest SubCensusZero fingerprints, wire into `build_signatures.py`.
8. Places (§9a): `place` column on all tables, per-place artifact dirs + global `signatures_dir`, dashboard/API/analysis take a `place` argument.
9. Field-map discovery (System §7b, passive): differential bitfield analysis + checksum search over the `events` corpus, ground-truth correlation against HA/MQTT; dashboard segment-labeling + propose `field_maps/` entry (no active confirmation — RX-only).
