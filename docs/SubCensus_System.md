# SubCensus — System & Shared Contract

Authoritative definition of everything shared between **SubCensusZero** (Flipper Zero FAP) and **SubCensusPi** (RTL-SDR / Raspberry Pi). **On any conflict, this document wins.** The two platform specs cover only platform-specific capture, UI, and build detail and defer here for the shared layer.

Related: `SubCensusZero_Spec.md`, `SubCensusPi_Spec.md`.

---

## 1. System overview — one system, three sensors
- **SubCensusZero** — portable Flipper Zero FAP. Narrowband CC1101; walk-around; on-device catalog + review; can TX (replay-to-identify). See `SubCensusZero_Spec.md`.
- **SubCensusPi** — stationary RTL-SDR/Pi service. Wideband; continuous `rtl_433` decode; web dashboard; RX-only. See `SubCensusPi_Spec.md`.
- **SubCensusEsp** — headless ESP32 + CC1101 node. Same narrowband CC1101 as the Zero (inherits its capture model + identical feature vectors); always-on and networked; serves review/label/config over WiFi; no display. See `SubCensusEsp_Spec.md`.

They share the **data + intelligence layer** (this doc) and differ only where the hardware demands it (§3).

---

## 2. Repo layout (monorepo)
```
subcensus/
  README.md
  shared/
    taxonomy.yaml              # device_class vocabulary — single source of truth (§5)
    schema/                    # column specs for fingerprints/protocol_map/occupancy/watchlist (§7, §9)
    docs/
      SubCensus_System.md      # this file
      SubCensus_Debug.md       # debug & test harness (all targets)
      SubCensusZero_Spec.md
      SubCensusPi_Spec.md
      SubCensusEsp_Spec.md
    core/                      # shared C logic (Zero+Esp), host-compilable + unit-tested
  tools/                       # host-side Python, used by BOTH (§8)
    build_signatures.py
    export_place.py
    analyze_place.py
    debug/                     # serial-RPC harness: flipper_screen/drive/serial (Debug spec §2)
    pyproject.toml
  test/                        # fixtures (.sub/.cu8/rtl_433 JSON) + core unit tests (Debug spec §1.2, §5)
  zero/                        # Flipper FAP (C, ufbt) — self-contained build
    application.fam
    subcensuszero.c
    scenes/…
    census_taxonomy.h          # GENERATED from shared/taxonomy.yaml (§10)
  pi/                          # RTL-SDR/Pi (Python)
    collector/  web/  systemd/
    pyproject.toml
  esp/                         # ESP32 + CC1101 headless node (PlatformIO)
    platformio.ini
    src/                       # capture (RMT), web server, storage
    data/                      # LittleFS web assets
```
`shared/` is the single source of truth; derived artifacts are **generated, not hand-maintained** (§10), so the two sides can't drift. Debug/test approach for every target: `SubCensus_Debug.md`.

> Publishing note: if the FAP is ever submitted to the official Flipper apps catalog, that catalog expects an app-shaped manifest/layout. Solvable from a monorepo (point the manifest at `zero/`, or git-subtree the FAP out) — a "later, if publishing" concern, not a reason to split.

---

## 3. Unified vs platform-native (the boundary)
**Unified — defined here, identical across both tools:**
- Places (§4), label taxonomy (§5), signature DB / classification brain (§6), feature-vector schema (§7), host-side tools (§8), shared data artifacts `occupancy.csv` / `watchlist.csv` / catalog record (§9).

**Platform-native — defined in each platform spec, deliberately not unified:**
- Capture/monitoring model (Zero: Recon/Sweep/Camp on a narrowband CC1101; **SubCensusEsp inherits this same CC1101 model**; Pi: wideband continuous `rtl_433` + optional multi-dongle).
- UI (Zero on-device review; Pi web dashboard; **Esp headless, browser-served over WiFi**).
- TX / replay-to-identify (Zero and, optionally, Esp — both CC1101; Pi is RX-only).
- Modulation handling (Zero/Esp juggle OOK/FSK presets; `rtl_433` resolves modulation itself).

The rule of thumb: **unify the brain and bookkeeping; keep the senses and controls native.**

---

## 4. Places
A **Place** is a named location profile (Home, Truck, Office, a POTA site, a hotel…). RF environments differ per location, so each place keeps its own occupancy/watchlist/captures/log; the classification brain (§6) is **global**, shared across all places and both tools.

- **`place_id`** = filesystem-safe slug of the name + short hash (unique, rename-safe). A `Default`/`home` place exists out of the box.
- **Per-place:** `occupancy.csv`, `watchlist.csv`, captures, and the capture/event log.
- **Global (not per-place):** the `signatures/` brain (§6).

**On-disk layout**
- *Zero (SD card):*
  ```
  /ext/apps_data/subcensuszero/
    signatures/                 # GLOBAL
    places/<place_id>/{place.meta, occupancy.csv, watchlist.csv, census_log.csv, captures/}
  ```
- *Pi:*
  ```
  signatures_dir/               # GLOBAL
  places_dir/<place>/{occupancy.csv, watchlist.csv, iq/}
  <db>.sqlite                   # devices/events/unknowns rows carry a `place` column
  ```
Active place: Zero stores it in `config.settings`; Pi in `config.yaml` (`place:`). A Zero place folder and a Pi place are interchangeable inputs to the analysis tools (§8).

---

## 5. Label taxonomy (`shared/taxonomy.yaml`)
One `device_class` vocabulary, single source of truth:
```
garage, car-fob, tpms, weather, doorbell, pir-motion,
energy-meter, water/gas-meter, remote, thermostat,
smart-home, beacon, unknown, other
```
Consumed by both tools; the Zero's `census_taxonomy.h` is generated from this file (§10).

---

## 6. Signature database (the classification brain)
Global, plain-file, append-friendly, at the `signatures/` root. Two tiers:

- **`protocol_map.csv`** — decoded-protocol / device-class → friendly name, `device_class`, typical use, notes. Handles the *decodable* cases (receiver/`rtl_433` returns a protocol → look up a human name).
- **`fingerprints.csv`** — feature-vector library (§7) for *raw / undecodable* cases and for disambiguating generic protocols.
- **`field_maps/`** (optional) — per-protocol field layout + checksum/CRC algorithm (which bits are `address`/`data`/`id`/`channel`/`checksum`, etc.), seedable from `rtl_433` decoder documentation + Flipper protocol definitions. Reference knowledge used by SubCensusZero's structured field editor (Zero spec §6) to present labeled bit positions and recompute checksums; not required for capture or classification.

**Classification pipeline (per capture, both tools):**
1. Known protocol decodes → look up `protocol_map` → propose name+class, `match_source=decoder`, high confidence; keep any ID field for disambiguation.
2. Else compute the feature vector (§7) → **gated k-NN** against `fingerprints.csv`: require `freq_bin` and `modulation` to match, then nearest on timing features (normalized weighted Euclidean). Propose top-N candidates + distance-derived confidence, `match_source=fingerprint`.
3. Write the best candidate into the catalog record's `match_*` fields. **Never auto-relabel** — advisory only; the user confirms.

**Active-learning loop:** when the user confirms/sets a `label`, append that capture's feature vector to the **global** `fingerprints.csv` with `source=user` — regardless of which place or which tool it came from. The brain gets smarter with use.

**Confidence & honesty:** every match carries a score + source, surfaced in the UI. Matching is on envelope/timing, not payload — so rolling-code/encrypted devices match only weakly (flag low confidence), and common-silicon devices (generic PT2262 remotes, etc.) collapse into a *family*; disambiguate by decoded ID bits **and by signal cadence (§7a)** when available (e.g. an identical-waveform periodic sensor vs. a one-shot button remote separate cleanly on cadence). Gate k-NN on freq+modulation first for robustness.

**Cadence as a soft feature:** signal cadence (§7a) is a **booster/disambiguator, never a hard gate** — a device seen once has no cadence yet. When both the query and a candidate have a cadence estimate, agreement (same `cadence_class`, and period within tolerance) boosts confidence; disagreement penalizes. It also drives anomaly detection: an unexpected periodic emitter, or a known device whose cadence suddenly shifts, is flagged for the analysis pass (§8).

---

## 7. Feature vector (canonical schema)
`fingerprints.csv` columns (identical on both tools):
```
id, freq_bin, modulation, sym_dur_us[1..3], n_symbols,
est_bitrate, preamble_len, repeat_count, device_name, device_class, source,
cadence_class, period_s, period_regularity, cadence_samples
```
Waveform fields: `freq_bin` (carrier binned, e.g. 5 kHz), `modulation` (OOK / 2-FSK / TPMS-preset), `sym_dur_us[1..3]` (1–3 dominant symbol durations from clustering the pulse-width histogram), `n_symbols`, `est_bitrate`, `preamble_len` (leading regular pulse run), `repeat_count` (frames per event).
Cadence fields (optional — see §7a): `cadence_class`, `period_s`, `period_regularity`, `cadence_samples`. **May be empty** for a freshly-captured unknown; they are populated as reception evidence accumulates and are used as a *soft* matching feature (§6), never a gate.

**Comparable across sensors:** the Zero derives the waveform fields from the CC1101 RAW async-RX pulse stream; the Pi derives them from saved IQ/pulse data (`rtl_433 -A` / `.cu8`). Both must bin frequency and normalize timing the same way so vectors from either tool are matchable in the same k-NN space. (Platform specs describe their own extraction; the *schema and normalization rules here* are binding.)

---

## 7a. Signal cadence (temporal fingerprint)
Many ISM devices have a characteristic emission schedule — weather/environmental sensors ~30–60 s, utility/tank meters ~5 min or hourly, TPMS motion-triggered then silent, door/PIR/remotes event-driven or one-shot. Cadence is a strong **class** discriminator and a disambiguator for identical waveforms (§6).

**It is a per-device property, not per-capture.** Derive it from the *reception history* of a grouped device (by `device_id`/signature), not from a single RAW capture.

**Fields**
- `cadence_class` ∈ { `periodic`, `quasi-periodic`, `event-driven`, `near-continuous`, `seen-once` }.
- `period_s` — estimated fundamental period (null for event-driven/seen-once).
- `period_regularity` — 0–1 (1 = metronomic); derive from the dispersion of inter-arrival intervals (e.g. `1 − min(1, CoV)`), so tight intervals score high.
- `cadence_samples` — number of intervals behind the estimate (evidence weight; low counts flag a shaky estimate).

**Dropout-robust estimation (important).** Missed receptions (narrowband hopping, packet loss, being tuned elsewhere) make raw intervals appear as 2×/3× harmonics of the true period, so a naive median is biased. Estimate the **fundamental**: build a histogram of inter-arrival deltas and take the smallest strongly-supported cluster / gcd of clusters, or a light autocorrelation over a binned reception timeline. Classify: tight single fundamental → `periodic`; fundamental with wide spread → `quasi-periodic`; no fundamental, clustered/Poisson arrivals → `event-driven`; effectively always-on → `near-continuous`; one reception → `seen-once`.

**Where computed.** Live sensors maintain a **running per-device estimator** (last_ts, count, running mean/variance, and a small interval histogram for harmonic folding) so the dashboard/UI shows current cadence without storing every event. The host-side merge (`build_signatures.py`, §8) recomputes a canonical, dropout-robust cadence from pooled event history when combining tools' data. On flash-limited sensors keep only the compact running estimator, not a full event log.

---

## 7b. Field-map discovery (reverse-engineering unknowns)
Turns an *unknown* signal into a characterized one by deriving its frame structure — promoting it toward a `field_maps/` entry (§6). Three layers, cheapest/safest first; the first two are **passive (no transmission)** and lean on the census corpus (many captures of the same device over time).

1. **Differential bitfield analysis (passive).** Across a device's capture corpus, align frames and score each bit's change-rate/entropy. Segment into: **static** (id/address/preamble), **slow-varying** (sensor values), **every-frame** (sequence/counter), and a **trailing block that flips whenever the payload changes** (candidate checksum). Auto-proposes a candidate structure with no user hypothesis and no TX.
2. **Checksum discovery (passive).** Brute the trailing block against the known checksum/CRC family (CRC-8 variants, XOR, sum, LFSR — the `rtl_433`/reveng set) over the static+payload span to *name* the algorithm, so edited frames can be re-signed.
3. **Ground-truth correlation (passive).** Regress a slow-varying field against a known-moving quantity — an HA/MQTT sensor value, time of day, or the device's own cadence (§7a) — to attach semantics ("these bits track temperature ×10").

**Interactive layer (any sensor with a suitable UI).** The differential result seeds a structured-editor overlay on an unknown; the user labels the segments (on-device on the Zero, in the browser on the Esp/Pi). **Active confirmation** — transmitting an edited frame to one's *own* device and observing the response — is available only on **TX-capable sensors (Zero, Esp)**, under the same own-device / TX-allow-list / single-frame guards. The **Pi is RX-only**, so it does passive analysis + labeling only (no active confirmation). The passive layers stand alone on all three; the Pi is typically the **strongest** at them (largest continuous corpus, and HA/MQTT ground-truth co-located for correlation).

**Output & loop.** A confirmed structure is written as a `field_maps/` entry (+ the device's `protocol_map`/fingerprint label), so a reverse-engineered device enriches the brain for **all** sensors. This is a natural LLM task (§8): hand the model the per-bit change profile, the CRC guess, and the ground-truth correlations, and it proposes the field map for user confirmation. **Never auto-commit** a derived field-map — propose, user confirms.

---

## 8. Host-side tools (`shared/tools/`, Python)
Nothing here runs on the Flipper; all operate on the shared artifacts. Each accepts a **SubCensusZero place folder or a SubCensusPi place** (query its SQLite into the same rolled-up bundle).

**`build_signatures.py`** — assembles/merges the brain. Seeds `protocol_map.csv` from the Flipper SubGhz protocol registry + `rtl_433`'s device catalog (freq/modulation/bit-length/fields as reference metadata; mind `rtl_433`'s GPL — reference parameters, don't copy code into the FAP). Merges user-labeled fingerprints from **both** tools. Runs the passive **field-map discovery** (§7b) over each device's capture corpus — differential bitfield analysis + checksum-algorithm search — to propose candidate `field_maps/` entries. Single merge point for the brain. **Proposes, never auto-commits** derived structures.

**`export_place.py`** — place → analysis bundle + prompt. Rolls the raw CSVs/SQLite into a compact, token-budgeted bundle: manifest; occupancy digest (top active bins + coverage gaps); device roll-up (grouped, Identified vs Needs-ID) with decoded IDs, match candidate, **and each device's cadence (§7a: class, period, regularity, sample count)**; full feature vectors for unknowns; reference grounding (ISM band→typical-device table **+ typical cadences per class** — e.g. weather ~30–60 s, meters ~5 min/hourly, TPMS motion-triggered, door/PIR event-driven — + relevant `protocol_map` slice); optional cross-place context (e.g. Home baseline). Emits a paste-able `prompt.md` and/or an API `messages` array. System-prompt intent: RF/ISM analyst; separate confident IDs from guesses; justify each from freq+modulation+timing+**cadence**→family; flag anomalies neutrally (including cadence anomalies); propose concrete next captures; return structured JSON + readable summary.

**`analyze_place.py`** — automates the round-trip. Provider-agnostic (`--provider anthropic|openai-compatible --base-url --model`) so it runs against the API **or local inference**; default local for home RF data (a map of your home's wireless devices is sensitive — cloud is opt-in). Requests structured JSON:
```
{ inventory:[...],
  identifications:[{signature, candidate, confidence, reasoning}],
  field_maps:[{signature, fields:[{name, bits, semantics}], checksum, confidence, reasoning}],
  anomalies:[...], coverage_gaps:[...], recommended_actions:[...] }
```
The `field_maps` come from feeding the model the §7b differential bit-change profile, CRC guess, and ground-truth correlations for unknowns — proposals only, surfaced for user confirmation, then written to `field_maps/`.
Writes `analysis.json` + rendered `analysis.md` into the place; re-runnable, diffs vs the prior analysis. **Label feedback (human-in-the-loop):** high-confidence identifications become *proposed* labels; a confirm step (or `--apply` gated by a confidence floor) writes confirmed ones into the global brain via `build_signatures.py`. Never silent auto-relabel (§6).

Optional future hook: expose `analyze_place` as an MCP tool so an agent can query a place's inventory directly.

---

## 9. Shared data artifacts & schemas
Identical columns on both tools so a place is interchangeable.

- **`occupancy.csv`** (per place): `freq_hz, noise_floor, peak_rssi, occupancy, crossings, last_seen`. Zero produces it via Recon Stage A (stepped RSSI sweep); Pi via the `rtl_power`/`soapy_power` heatmap pass — different engine, same artifact.
- **`watchlist.csv`** (per place): `freq_hz, modulation, threshold_dbm, occupancy, source`. Zero's Sweep/Camp consume it; Pi uses it to prioritize bands / assign dongles.
- **Catalog record (core, shared fields):** `ts, freq_hz, modulation, device_class, first_seen, last_seen, count, match_name, match_class, match_conf, match_source, label, cadence_class, period_s, period_regularity, cadence_samples`. The `cadence_*` fields (§7a) are the per-device temporal fingerprint, maintained by whichever sensor observes the device. Platforms may extend the record (Zero adds `preset, fsk_suspected, sub_file, rssi_dbm, duration_ms`; Pi keeps richer per-event rows in SQLite), but the core fields above are common and are what the analysis tools rely on.

**Recon lifecycle (review / accumulate / reset — all per place).** Recon is probabilistic (a single pass misses periodic emitters that didn't fire while their bin was being sampled), so these artifacts are **cumulative, not single-shot**:
- **Review:** the artifacts are inspectable — a ranked view of `occupancy.csv` (hot bins: freq · peak RSSI · occupancy % · crossings · last-seen) and the derived `watchlist.csv`. Per-entry actions: **pin** or **exclude** an entry (persisted in `watchlist.csv` as `source=user-pin` / `user-exclude`), or jump straight to Camp on it.
- **Accumulate (default for a re-run):** a subsequent Recon pass **merges** into the existing `occupancy.csv` per bin — rolling/max peak RSSI, summed crossings, recomputed occupancy, updated last-seen — so coverage improves with each pass. The regenerated `watchlist.csv` preserves user pins/exclusions.
- **Fresh / Reset:** an explicit, confirm-gated action clears **this place's** `occupancy.csv` + `watchlist.csv` to start clean (moved location, changed antenna, RF environment shifted). Reset prompts whether to **keep or wipe user pins/exclusions**. It touches recon artifacts only — captures, `census_log`, and the global `signatures/` brain are untouched.

**No valid recon — monitoring is never hard-gated on it.** The watchlist is an *optimization*, not a prerequisite. A place with an absent/empty `watchlist.csv` still monitors:
- **Fallback:** Sweep/Camp fall back to the configured **preset frequency list** (the documented `watchlist if present, else preset` behavior).
- **Camp:** always available — it needs one explicit frequency (from the allowed list), which requires no recon. Only the **Auto = busiest-watchlist** pick depends on recon and is disabled/greyed when there's no watchlist.
- **Sweep:** available, but it's the mode that benefits most from a watchlist (tight revisit → fewer missed one-shots), so without one it shows a **non-blocking** hint ("No recon for this place — sweeping the default band list; Run Recon to focus") with Proceed / Run-Recon-now. Dismissable, never a wall.
- **"Valid" = present and non-empty.** Only absent/empty triggers the fallback. **Staleness** (moved location, weeks old) does **not** change whether Sweep/Camp run — it surfaces as a separate soft "recon is N days old — re-run?" hint.

---

## 10. Single source of truth & codegen
To make drift impossible:
- `shared/taxonomy.yaml` → generates the Zero's `census_taxonomy.h` and the Pi's taxonomy loader.
- `shared/schema/` → generates CSV headers / validators for `fingerprints.csv`, `protocol_map.csv`, `occupancy.csv`, `watchlist.csv`.
A schema change lands in one place and both consumers are regenerated in the same commit — the whole point of the monorepo.
