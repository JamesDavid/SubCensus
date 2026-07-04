# SubCensusZero — Flipper Zero Sub-GHz Survey & Logger

**Target:** Flipper Zero FAP (C, built with `ufbt`)
**Author intent:** Census of ISM-band signals around a home. The scanning/capture loop is **passive** (never transmits while surveying). Capture activity to standard `.sub` files with metadata, make later classification easy, and let the user **replay their own captures on demand** to identify which physical device is which. Portable walk-around companion to the stationary RTL-SDR tool (separate spec).

> Working name: **SubCensusZero**. `appid: subcensuszero`.

> **Shared contract lives in `SubCensus_System.md`** — Places, the signature DB / classification brain, the feature-vector schema, the label taxonomy, the host-side tools (`build_signatures`/`export_place`/`analyze_place`), the shared `occupancy.csv`/`watchlist.csv` artifacts, and the repo layout are all defined there and are authoritative. This spec covers only Zero-specific capture, UI, and build; overlapping sections below reference the System doc and defer to it on any conflict.

---

## 0. Phase 0 — Prior art to study first (do this before writing code)

The individual building blocks already exist in stock firmware and community apps; nobody has combined them into an unattended multi-band census with a persistent catalog + label workflow (that's the new part). **Start by reading these and reuse proven code rather than reinventing it.** Produce a short written findings note mapping each reference to the SubCensusZero module it informs before beginning implementation.

| Reference | What to take from it |
|---|---|
| **ProtoView** — antirez — https://github.com/antirez/protoview | Closest existing app. Study its **protocol-decoder framework** (signal delivered as a pulse bitmap; helpers to seek sync/preamble, decode line codes, verify checksums) and its handling of **TPMS / weather / remote** modulations. It already documents the OOK vs 2-FSK vs TPMS-modulation guidance and an rtl_433 `.cu8` capture-to-file dev loop. This is the single best reference for our §5.3 dual-preset handling and the "unknown" bucket. |
| **Read RAW (built-in)** — `flipperzero-firmware/applications/main/subghz/scenes/subghz_scene_read_raw.c` | The Camp-mode capture engine already exists here: async RX → RAW file-encoder → `.sub`, gated by an RSSI threshold. Base §5.1 capture on this. |
| **Frequency Analyzer (built-in)** — `.../subghz_frequency_analyzer_worker.c` | The Sweep detection loop: RSSI sampled across the configured frequency list, peak reported. Base §3.1 sweep on this. |
| **Spectrum Analyzer (built-in FAP)** | Stepped RSSI sweep across a band with a waterfall/bar display. The mechanism for §3.3 Recon Stage A — but SubCensusZero logs/aggregates the sweep into an occupancy map + watchlist rather than only drawing it. |
| **Read (built-in)** — `SubGhzReceiver` + `SubGhzEnvironment` protocol registry | The opportunistic known-protocol decode/tagging path (§5.1 auto-classify). |
| **Weather Station** (Skorpionm) & **TPMS** (wosk) — https://github.com/wosk/flipperzero-tpms | How to load an alternate protocol set and run continuous decode. Note the gap we're closing: the Weather Station app finds sensors but **can't save** them — SubCensusZero must persist and catalog. |
| **FlipRSDR** (2026) — raw sub-GHz timing capture FAP + desktop receiver/analyzer | Closest existing *capture+analyze workflow*. Study its async-RX timing-capture path (burst start/continuation/completion from idle-gap behavior) and its "preserve timing fidelity first, decode later" stance — directly relevant to §5.1 capture and the §5.5 feature vector. Note the architectural contrast: FlipRSDR is **PC-tethered** (desktop does the analysis live); SubCensusZero keeps an on-device catalog and does LLM analysis as a host-side batch step (§5.7). |
| **Subdriving** (Xtreme firmware + GPS add-on, e.g. SCE TinyGPS) | Closest existing take on the **per-place** idea: geo-stamps received sub-GHz signals to map them. SubCensusZero generalizes this from "where" (GPS coordinates) to named **Places** with their own occupancy/watchlist/catalog (§5.6). If wiring an external GPS is ever wanted, this is the reference for the location tag in `place.meta`. |
| **jamisonderek Sub-GHz wiki** — https://github.com/jamisonderek/flipper-zero-tutorials/wiki/Sub-GHz · **flipc.org** (web `.sub` analyzer) | `.sub` file-format reference and an offline way to verify captures during development. |
| **Sub-GHz app source** — https://github.com/flipperdevices/flipperzero-firmware/tree/dev/applications/main/subghz | Ground truth for current SDK symbol names/signatures — **pin against the firmware actually on the device** (§8). |

Deliverable for Phase 0: the findings note (reference → module map) + confirmation of which firmware/SDK the build targets, so §5/§6 use the right APIs. Do not start Milestone 1 until this is done.

---

## 1. Goals & non-goals

**Goals**
- Continuously monitor sub-GHz ISM activity in one of two user-selectable modes (Sweep / Camp).
- On detected activity, capture raw signal to a standard `.sub` file and append a row to a CSV index with `freq, timestamp, RSSI, duration, preset, decoded-protocol (if any)`.
- Opportunistically tag captures with a known protocol name when a decoder matches; leave the rest as `unknown` for later review.
- On-device review scene to browse captures and assign a label, **plus** CSV export for labeling on a computer.

**Constraints & scope**
- **Monitoring is passive.** The Sweep and Camp loops only receive — the tool never transmits while surveying. (You don't want a census tool keying up mid-scan.)
- **Replay is allowed, deliberate, and manual.** From the Review scene the user can retransmit one of *their own* captures on its stored frequency/preset to confirm/identify the device — the same thing the stock Sub-GHz app already does with any `.sub`. This is the only TX path, it is never automatic, and it is bounded by the firmware's **TX allow-list** (§6). Replay-to-identify directly serves classification: play it, see what responds, label it.
- **Out of scope (not this tool's job):** rolling-code defeat, brute-force / de Bruijn sweeps, jamming, de-hopping. Rolling-code replay wouldn't work from a passive capture anyway; nothing to gate there.
- Not an SDR. See §3.

---

## 2. Why the design looks the way it does (read this first)

The Flipper's radio is a **CC1101 — narrowband**. It hears **one frequency at a time** with a configurable RX bandwidth (~58–812 kHz), not the whole band at once. Two consequences shape everything:

1. "Constantly monitor ISM" must be implemented as **scanning** (hop a freq list) or **camping** (park on one band). There is no simultaneous whole-band capture.
2. Detection (RSSI/power) is roughly modulation-agnostic, but **capture/decode needs the correct modulation preset** (OOK vs 2-FSK). A signal captured under the wrong preset will not replay or decode cleanly. See §5.3 — this is the #1 subtlety.

Coverage math to encode in the UI/help text: most **button-press** transmissions last a few hundred ms and fire once; most **sensors/weather stations** beacon periodically (≈30–60 s). Sweeping across N frequencies with dwell `d` gives a revisit period ≈ `N·d`; a one-shot fired while parked elsewhere is missed. So: **Sweep** is for catching periodic beacons over time; **Camp** is for reliably catching one-shots on a chosen band.

---

## 3. Modes (Recon → Monitor pipeline)

Three modes sharing one capture/logging engine (§5). The intended flow is a **pipeline**: run **Recon** once to learn what's actually active in *this* environment, which writes a `watchlist`; then **Sweep** or **Camp** monitor using that watchlist (small N, modulation + threshold pre-solved). Sweep/Camp also run standalone off the static preset list if you skip Recon.

### 3.1 Sweep (census)
- Cycle a frequency list — **the Recon `watchlist` if present, else the configured preset list**. Dwell `dwell_ms` on each, sampling RSSI.
- If `RSSI ≥ threshold` (per-frequency threshold from the watchlist when available, else global/Auto) → enter **Capture** on that frequency (§5). After capture completes (or `capture_max_ms` elapses), resume sweeping at the next frequency.
- Default preset list (US / Tempe): `315.00, 390.00, 433.92, 915.00` MHz. (868 is mostly EU — offer as a preset but off by default.) Validate every entry against the firmware's allowed-RX list.
- **Why the watchlist matters:** revisit period ≈ N × dwell, so pruning N to only active frequencies tightens revisit and cuts missed one-shots.
- **No valid recon (System §9):** never blocked — falls back to the preset list and shows a **dismissable** hint ("No recon for this place — sweeping the default band list; Run Recon to focus" · Proceed / Run Recon now).

### 3.2 Camp (button-catcher)
- Fix one frequency — manually chosen, or **auto-pick the busiest `watchlist` entry** — continuous RSSI monitor.
- `RSSI ≥ threshold` → Capture → resume monitoring the same frequency.
- Best temporal coverage for one-shots on a single band.
- **No valid recon (System §9):** always available — needs only an explicit frequency (from the allowed list), which requires no recon. Only the **Auto = busiest-watchlist** option is disabled/greyed when there's no watchlist; manual frequency selection is unaffected.

### 3.3 Recon (discovery) — feeds Sweep/Camp
Learns the local active frequencies so the monitor phases target real activity instead of a static guess. Not an SDR sweep — it's fast narrowband RSSI hopping across a candidate grid, so discovery resolution = grid granularity. Best at finding **always-on / periodic** emitters (weather, sensors, TPMS while rolling); on-demand remotes still need a Camp-and-press pass. Three stages:

**Stage A — Occupancy survey.** Stepped RSSI sweep across the CC1101 legal segments only (300–348 / 387–464 / 779–928 MHz) on a **hybrid grid**: known common channels at fine resolution (e.g. 303.875, 310, 315, 318, 345, 390, 418, 433.42/.92/434.42, 868.35, 915-band) **plus** a coarse background grid (~250 kHz steps; set RX BW ~200–270 kHz to tile without gaps) as a safety net for the unexpected. Over a `survey_minutes` window (many passes), accumulate per-bin stats: rolling noise floor, peak RSSI, occupancy fraction (samples above floor+margin), threshold-crossing count, last-seen. Output: a ranked **activity map** (`occupancy.csv`).
  - Coverage note to surface in-app: a sensor beaconing every ~60 s only shows up if the sweep is on its bin during a beacon, so cumulative catch probability rises with survey length — recommend minutes-to-an-hour, longer to be thorough.

**Stage B — Refine + modulation sniff.** For each hot bin above an activity cutoff, fine-sweep ±a few hundred kHz to lock the true carrier (devices sit off-nominal). Then capture short windows under **OOK and 2-FSK** (reuse §5.3 Dual logic) and keep whichever decodes or yields cleaner pulse structure — that resolves modulation, which RSSI alone can't. Snap the result to the nearest sensible channel.

**Stage C — Emit watchlist.** Write `watchlist.csv`: `freq_hz,modulation,threshold_dbm,occupancy,source(recon|user-pin|user-exclude)`. User can pin/exclude entries. Sweep/Camp load this. Per-band thresholds come from Stage A noise floors (adaptive, replacing the single global Auto threshold where a watchlist entry exists).

**Lifecycle (per place — see System §9).** Recon is cumulative:
- **Run offers Accumulate (default) or Fresh.** A re-run **merges** into the existing `occupancy.csv` per bin (max/rolling peak RSSI, summed crossings, recomputed occupancy, updated last-seen) so coverage improves each pass; the regenerated `watchlist.csv` preserves user pins/exclusions. **Fresh** clears first (same as Reset).
- **Recon results scene** (§6) reviews the accumulated `occupancy.csv`/`watchlist.csv`.
- **Reset recon** (§6, confirm-gated) wipes this place's `occupancy.csv` + `watchlist.csv`; prompts keep-or-wipe user pins. Captures/`census_log`/global `signatures/` untouched.

Reference: model the stepped-sweep on the built-in **Spectrum Analyzer** FAP (§0) — same mechanism. SubCensus **both** renders it live (the Recon live view's spectrum strip, §6) **and** logs/aggregates it into `occupancy.csv` + the watchlist, where the built-in app only displays.

---

## 4. Settings (VariableItemList scene)

| Setting | Values | Notes |
|---|---|---|
| Place | active place, + Manage | Per-location profile (§5.6); Manage → New/Rename/Delete/Set active |
| Mode | Recon / Sweep / Camp | |
| Frequency preset | US ISM / EU ISM / Custom | Custom → edit list. Fallback list when no watchlist |
| Use watchlist | on/off | Sweep/Camp load Recon's `watchlist.csv` when present (§3.3); on by default |
| Camp frequency | from allowed list, or **Auto (busiest watchlist)** | Camp mode only |
| Recon grid | Hybrid / Known-only / Full-band | §3.3 Stage A. Default Hybrid |
| Recon step / RX BW | ~250 kHz / ~200–270 kHz | Coarse background grid granularity |
| Survey minutes | 1–120 | Longer = higher catch prob for periodic beacons. Default 15 |
| RSSI threshold | −100…−40 dBm, or **Auto** | Auto = sample noise floor for ~2 s, set threshold = floor + margin (default +12 dB) |
| Capture preset | AM/OOK 650, AM/OOK 270, FSK 2-FSK dev, **Dual (OOK+FSK)** | See §5.3 |
| Dwell (Sweep) | 20–500 ms | Default 80 ms |
| Capture max | 200–5000 ms | Default 1500 ms |
| Signal-end gap | 20–500 ms | End capture after this much sub-threshold quiet |
| Min gap between captures | 0–5000 ms | Dedup repeated frames (§7) |
| Auto-classify | on/off | Attempt known-protocol decode during capture |
| Match against DB | on/off | Check captures against the signature DB (§5.5); on by default |
| Notify on capture | LED / LED+vibro / off | |

Persist settings to `/ext/apps_data/subcensuszero/config.settings`.

---

## 5. Capture engine (shared)

### 5.1 Flow
1. Tune CC1101 to hit frequency + configured RX bandwidth + capture preset.
2. Start async RX. Fan the received level/duration stream to **two** sinks:
   - **RAW file encoder** → writes `.sub` (standard Flipper RAW format, so existing tools/replay work).
   - **Protocol receiver** (`SubGhzReceiver` loaded with the standard `SubGhzEnvironment` decoders) when *Auto-classify* is on → yields a protocol name/key if a known device matches.
3. End capture when signal drops below threshold for `signal_end_gap` ms, or `capture_max` reached.
4. Write the `.sub`, append the index row, fire notification.

> Implementation note for Claude Code: model the RAW capture path on firmware **Read RAW** (`applications/main/subghz/scenes/subghz_scene_read_raw.c`, `SubGhzProtocolDecoderRAW`, file-encoder worker) and the decode path on **Read** (`SubGhzReceiver` + `subghz_receiver_decode`). The RSSI sweep/peak logic mirrors **Frequency Analyzer** (`subghz_frequency_analyzer_worker.c`, `furi_hal_subghz_get_rssi`). Exact symbol names/signatures **depend on firmware + SDK version** — pin to the target firmware's SDK and adjust; do not assume the signatures here are current. On newer firmware the radio is behind the `subghz_devices_*` abstraction (internal CC1101 vs external); use it if present.

### 5.2 Timestamps
Use the RTC (`furi_hal_rtc_get_datetime`) so index rows and filenames carry real wall-clock time.

### 5.3 OOK vs FSK — the important subtlety
RSSI detects *power*, so a hit fires regardless of modulation, but the `.sub` is only clean if captured under the matching preset. Cheap remotes/PIR/doorbells are usually **OOK**; many weather stations and **TPMS are 2-FSK**.

Implement a **Dual** capture preset: on a hit, capture a short window under OOK, and if the protocol decoder produces nothing *and* RSSI stayed strong, immediately re-capture the next occurrence under 2-FSK. Tag the index row with the preset actually used and set `fsk_suspected=1` when OOK decode failed on a strong signal. This makes the "unknown" bucket far more useful later.

### 5.4 Files & index (per-place layout)
Storage is namespaced by **Place** (§5.6). Global data (the classification brain) sits at the root; everything location-specific lives under the active place.
```
/ext/apps_data/subcensuszero/
  config.settings                     # global app settings + active place id
  signatures/                         # GLOBAL, shared across places + SubCensusPi
    protocol_map.csv
    fingerprints.csv
  places/
    <place_id>/
      place.meta                      # name, created, notes, optional location tag
      occupancy.csv                   # Recon Stage A activity map (per-place)
      watchlist.csv                   # Recon Stage C output (per-place)
      census_log.csv                  # capture index (per-place)
      captures/                       # .sub files (per-place)
```
- **Recon outputs (§3.3), per-place:** `occupancy.csv` (`freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen`) and `watchlist.csv` (`freq_hz,modulation,threshold_dbm,occupancy,source`). Sweep/Camp read the active place's `watchlist.csv`; `occupancy.csv` is for review/optional heatmap.
- **Capture:** `captures/YYYYMMDD_HHMMSS_<freqkHz>_<preset>.sub` (standard `.sub`, replayable/inspectable in existing tools).
- **Index (append per hit):** `census_log.csv`
  ```
  ts_iso,freq_hz,rssi_dbm,duration_ms,preset,fsk_suspected,protocol,key,match_name,match_class,match_conf,match_source,sub_file,label
  ```
  `protocol`/`key` empty when undecoded. `match_*` = best candidate from the classification DB (§5.5): device/family name, device class, confidence 0–1, and source (`decoder` | `fingerprint` | `user`). `label` is the user-confirmed final call (empty until confirmed).
- Flush the CSV after every row (long unattended runs — don't lose data on power-off).

### 5.5 Classification (Zero specifics)
The signature DB, `protocol_map`/`fingerprints` schemas, the classification pipeline, the active-learning loop, and the confidence/honesty rules are defined in **System §6–7** and are authoritative. Zero-specific points only:
- **Feature-vector source:** the Zero computes the canonical vector (System §7) from the **CC1101 RAW async-RX pulse stream** — cluster the level/duration pairs for the 1–3 dominant symbol durations; keep it cheap enough to run on-device per capture. Bin frequency / normalize timing exactly as System §7 requires so vectors match Pi-derived ones.
- **Where results land:** matches populate the `match_*` fields of the per-place `census_log.csv` (§5.4). On confirming a `label` in Review (§6), append the vector to the **global** brain per System §6 (global regardless of active place).
- **Cadence (System §7a):** the Zero is a **weak cadence measurer** — walk-around sessions rarely observe a device long enough to estimate a period. It records reception timestamps and contributes partial interval data, but mostly *consumes* `cadence_*` from the shared brain to help identify/disambiguate. Cadence measurement is a stationary-sensor (Pi/Esp) strength.

### 5.6 Places (Zero specifics)
Place model, per-place vs global split, and on-disk layout: **System §4** (Zero layout also shown in §5.4). Zero-specific behavior:
- **Management (GUI, §6):** New (text-input name) · Rename · Delete (confirm; removes the place folder only, never `signatures/`) · Set active. Show the active place prominently.
- **`place.meta`:** name, created, notes, optional location tag (manual text, or lat/lon if an external GPS is ever wired — not required).
- **Portability:** each place folder is self-contained — copy off the SD card to archive/share or hand to SubCensusPi; keep `signatures/` synced separately (shared brain).
- **Optional stretch:** a *Compare* view diffing two places' watchlists ("what's here that isn't at home?"). Not required for v1.

### 5.7 LLM analysis & host-side tools
`build_signatures.py`, `export_place.py`, and `analyze_place.py` are shared and fully specified in **System §8**; they run off-device and accept a Zero place folder or a Pi place. The Zero only *reads* the resulting `signatures/` CSVs and *emits* place folders for those tools to consume. (fccid.io / FCC OET cross-checks are a manual host-side enrichment step, per System §8.)

---

## 6. GUI (ViewDispatcher + scenes)

- **Main menu:** show active **Place** at top (tap → switch/manage) · Run Recon · **Recon results** · Start Sweep · Start Camp · Review captures · Settings · About.
- **Run Recon:** first prompts **Accumulate** (default) or **Fresh** (§3.3 lifecycle), then opens the Recon live view.
- **Recon live view (split screen):** the "surfing" view.
  - **Top pane (~40 px) — spectrum/activity strip:** RSSI bars across the segment currently being swept (frequency on X, RSSI on Y), **peak-hold-with-decay** so active bins stay briefly lit, a cursor at the current sample position, and a segment label. It **auto-follows** the sweep across the CC1101 segments (300–348 / 387–464 / 779–928) — 128 px can't show 300–930 MHz at once, so it shows one segment at a time and moves with the sweep. **Left/Right** optionally pages to inspect a segment manually. (Render models the built-in Spectrum Analyzer FAP, §0.)
  - **Bottom pane (~24 px) — status:** current segment · freq · hot-bin count · elapsed · pass count. Always visible.
  - **OK toggles the top pane** between the spectrum strip and a **live top-hits mini-list** (strongest N bins so far: freq · peak RSSI). Status line stays in both.
  - The full ranked activity map + watchlist live in the **Recon results scene** (below), not here — the live view is glance-feedback; results is what you act on.
- **Recon results scene:** ranked `occupancy.csv` (freq · peak RSSI · occupancy % · crossings · last-seen) and the derived `watchlist.csv` (freq · modulation · threshold · source). Per-entry: **Pin** / **Exclude** / **Camp here**. Scene action: **Reset recon** (confirm; prompts keep-or-wipe user pins — §3.3/System §9).
- **Place scene:** list places with active marked; New (text-input name) · Rename · Delete (confirm; removes the place folder only, never global `signatures/` (§5.6)).
- **Start Sweep / Start Camp — launch flow:** Sweep starts immediately on the active watchlist-or-preset list (§3.1). **Camp** first opens the **Camp frequency picker** (below), then starts.
- **Camp frequency picker:** choose the camp frequency from — **Watchlist** (pick a recon hot bin) · **Allowed presets** (315 / 390 / 433.92 / 915 …) · **Manual entry** (number-entry scene, validated against the RX allow-list) · **Auto** (busiest watchlist entry; greyed with no watchlist, §3.2).
- **Sweep view (live):** single-frequency **status** (no spectrum — one freq at a time): current freq, RSSI bar, hits counter, last match, elapsed; cursor/■ shows the active freq in the list. **OK toggles** a **recent-hits list** (last N: time · freq · match/unknown) so you see *what* just landed, not only a count; in the list, Up/Down scroll and OK on a row **jumps to that capture in Review** (worker keeps running). **Back** stops and returns. Optional: long-press OK to pause/resume.
- **Camp view (live):** same interaction on a fixed frequency: freq, RSSI bar (rolling graph if cheap), hits counter, last match; **OK toggles** the recent-hits list (same scroll/jump-to-Review); Back stops; optional pause. (The spectrum strip is Recon-only.)
- **On capture (Sweep/Camp):** the "● REC" overlay + LED/vibro (below) plus an inline flash of the freshest match/unknown tag. Captures flow to `census_log`/`captures` automatically — **labeling happens later in Review**, so the live loop is never interrupted by a modal.
- **Capture indicator:** brief LED flash (+vibro if enabled) on each capture; on-screen "● REC" during a capture window.
- **Review scene:** list rows from `census_log.csv` (freq · time · protocol/unknown · best match+confidence · label). Detail view shows metadata **and the top-N classification candidates** from the DB (§5.5) with their confidence and source; the **label picker** lets the user accept a candidate or pick from the shared taxonomy (System §5) and writes the `label` column in place. **On confirm, append the capture's feature vector to `fingerprints.csv` (source=user)** so the DB learns (§5.5 active-learning loop). Optional: trigger the standard `.sub` info view for the raw file.
- **Replay action (identify your own devices):** from a capture's detail view, retransmit the selected `.sub` on its stored frequency + preset — **once by default**, with an optional repeat count (small cap, e.g. ≤10). This is the workflow for classification: play a capture, watch which device in the house reacts, then label it. Reference the stock RAW send path (file-encoder worker → `furi_hal_subghz_start_async_tx`), same as Read RAW → Send. **Guard:** check the freq against the firmware's **TX** allow-list (`furi_hal_subghz_is_tx_allowed` or equivalent for the SDK version); if TX isn't permitted on the installed firmware for that band, grey out Replay for that row and show why. Replay is only ever reachable from an explicit user tap in Review — never from the Sweep/Camp loops.
- **Edit-before-transmit (own-device testing / protocol analysis):** an optional editor reachable from the same capture detail view, sharing Replay's TX-allow-list guard. Two tiers:
  - **Raw bit/hex edit** (any capture): render the `.sub` as decoded symbols under its preset; let the user flip bits / edit the payload hex; re-encode to timing on TX.
  - **Structured field editor** (protocol known — a decoder matched or the brain identified it): present the frame as **labeled fields** from a per-protocol **field-map** (System §6 — layout + checksum algorithm), e.g. PT2262 `sync/address[N]/data[M]`, a weather sensor `id/channel/flags/temp/humidity/checksum`. Edit a field → **recompute the protocol checksum/CRC** → re-encode. Before TX is allowed, **decode-back** the edited frame and show a before/after field diff; block TX if it doesn't re-decode cleanly.
  - **Field-map discovery on unknowns (reverse-engineering aid):** the same editor also opens on *unknown* signals to help identify them (System §7b). It seeds an overlay from the passive **differential bitfield analysis** (static / slow-varying / counter / checksum segments derived from the device's capture corpus), lets the user **apply a suspected structure and label segments**, and — optionally — **confirm actively** by transmitting an edited frame to their *own* device and watching it react ("set these bits → the displayed temperature changes → these are the temp bits"). A confirmed structure is proposed as a `field_maps/` entry (user confirms; never auto-committed), promoting the unknown toward characterized.
  - **Scope guards (design constraints, not afterthoughts):** the editor edits **only the single frame in view** — **no auto-increment, value sweeping, or address-space iteration** (keeps the no-brute-force/de Bruijn exclusion). Manual/explicit only; passive-while-scanning unchanged. Edited transmissions are logged **distinctly** from captures (an edited TX is not a census observation) so the catalog isn't polluted.
  - *Note on scope of effect:* this operates on fixed-code protocols and your own gear — a recomputed checksum is error-detection, not security, and bit-editing cannot forge a rolling code (a captured rolling code is stale; the next valid one needs the device secret). Frame the UI accordingly.
- **Frequency-list editor & manual entry:** reached from Settings (Frequency preset → **Custom**) and from the Camp picker's Manual option. Add/remove frequencies; **manual entry** is a number-entry scene (ByteInput/number-input style) validated against the firmware RX allow-list (reject out-of-band). Custom lists are per-place-independent app settings.
- **About scene:** app version; the firmware/SDK **API version it was built against** (so a mismatch is diagnosable); a one-line **passive-monitoring / RX note** (Sweep/Camp never transmit; Replay/Edit-TX are explicit, TX-allow-list gated); storage tier + active place; expected **battery drain** for long census runs; brief prior-art credits (§0).
- **Confirmation dialogs (common pattern):** all destructive or TX actions use one confirm widget with **No defaulted** for destructive ones — Delete place, Reset recon, Fresh recon, Replay, and Edit-before-transmit **Send**. TX confirms additionally show freq · preset · repeat count before sending.
- Keep the RX worker running while navigating live views; handle back/exit cleanly (stop async RX, close files).

Power: census runs are long. Let the screen dim normally but keep the worker alive; document expected battery drain in About.

### 6.1 Empty & error states
Every list/results view needs a sensible zero-data screen, and the SD failure modes must be handled (the app stores captures/config/places on `/ext`):
- **No recon yet** (Recon results / empty watchlist): "No recon for this place — Run Recon" with a direct Run-Recon action.
- **No captures yet** (Review, fresh place): "No captures yet — Start Sweep or Camp."
- **New place:** created empty — shows the no-recon and no-captures states above until it's populated.
- **SD card missing:** the app needs `/ext`. On no card, show a blocking "SD card required" screen; monitoring that writes is disabled, but About remains reachable. Recover automatically when a card is inserted.
- **SD full:** banner + stop writing captures; optionally keep logging RSSI-only "blip" rows (§7) so activity is still noted. Surface remaining space in About/Settings.
- **TX not permitted** on the installed firmware for a band: Replay/Edit-TX greyed with a reason (already handled per §6 Replay guard).
- **Firmware/API mismatch** (FAP built against a different SDK): fail gracefully with a message pointing at the API version shown in About, rather than crashing.

---

## 7. Edge cases to handle explicitly
- **Repeat suppression:** remotes send a frame many times per press. Collapse to one capture per event using `min gap between captures` + optional identical-decode dedup, so you get one `.sub` per press, not 30.
- **Sweep miss probability:** surface dwell/list-length tradeoff in the Sweep help text.
- **SD throughput:** buffer the RAW stream; flush index after each row.
- **Allowed-frequency guard:** reject/omit any freq not in the firmware RX allow-list for monitoring. The Sweep/Camp loops never transmit regardless; **Replay (§6) is the only TX path** and is separately gated by the firmware TX allow-list.
- **Empty/aborted captures:** if a hit doesn't produce a minimum number of edges, discard the `.sub` but still log an RSSI-only "blip" row (helps map busy-but-uncapturable spots).

---

## 8. Build & repo

- Toolchain: `pip install ufbt`; `ufbt update` (**pin the channel/SDK to the firmware actually on the device** — release/rc/dev, or the custom firmware's SDK if running Unleashed/Momentum, so sub-ghz APIs match). Scaffold: `ufbt create APPID=subcensuszero`. Build: `ufbt`. Deploy/run: `ufbt launch`. Debug: `ufbt cli` / `ufbt lint`.
- `application.fam`: `appid, name, apptype=FlipperAppType.EXTERNAL, entry_point, requires=["gui"], stack_size, fap_category="Sub-GHz", fap_icon, fap_icon_assets`.
- Suggested layout:
  ```
  subcensuszero/
    application.fam
    subcensuszero.c            # entry, view_dispatcher, scene manager
    scenes/                # menu, settings, sweep, camp, review
    census_worker.{c,h}    # RSSI sweep + async RX capture + fan-out
    census_log.{c,h}       # csv index + .sub writing helpers
    census_taxonomy.h      # label list (shared vocab)
    icons/
  ```

## 9. Milestones
0. **Phase 0 — Prior art (§0):** read the reference implementations, write the reference→module findings note, and pin the target firmware/SDK. Gate: do not start Milestone 1 until this is complete.
1. Skeleton FAP: menu + settings persistence + allowed-freq validation + **Places** (create/switch/rename/delete, per-place storage layout §5.4/§5.6, default place on first run).
2. Camp mode: RSSI monitor → OOK RAW capture → `.sub` + CSV row + notify.
3. Sweep mode: freq cycling + per-freq RSSI + trigger reuse of the capture engine.
4. Recon mode (§3.3): stepped occupancy sweep (hybrid grid) → `occupancy.csv`, refine + OOK/FSK modulation sniff, emit `watchlist.csv`; Sweep/Camp load the watchlist + adaptive per-band thresholds.
5. Auto-classify: wire `SubGhzReceiver`/environment for protocol tagging.
6. Classification DB (§5.5 + System §6): `protocol_map` lookup for decoded hits, feature-vector + gated k-NN against `fingerprints.csv` for raw hits, candidates surfaced in Review, confirm-appends-fingerprint loop, host-side `build_signatures.py`.
7. Dual OOK/FSK capture + `fsk_suspected` flag.
8. Review scene + on-device labeling + **replay-to-identify** (TX-allow-list guarded) + repeat-suppression polish.
9. Host-side `export_place.py` + `analyze_place.py` (§5.7): place → bundle + prompt, provider-agnostic structured analysis to `analysis.json`/`.md`, confidence-gated label feedback into the brain.
10. (Optional) Edit-before-transmit + field-map discovery (§6, System §7b): raw bit/hex edit; structured field editor for known protocols (field-map + checksum recompute + decode-back gate); differential-analysis-seeded overlay on unknowns with segment labeling + optional own-device active confirmation → proposed `field_maps/` entry. Single-frame only, TX-allow-list guarded, edited-TX logged distinctly.
