# SubCensusZero — Flipper Zero FAP

Portable narrowband (CC1101) ISM survey + logger. Passive Recon/Sweep/Camp monitoring,
capture to standard `.sub` files with a persistent catalog + on-device label workflow, and
**replay-to-identify** (the only TX path — explicit, manual, TX-allow-list gated). Spec:
[`../docs/SubCensusZero_Spec.md`](../docs/SubCensusZero_Spec.md); shared contract:
[`../docs/SubCensus_System.md`](../docs/SubCensus_System.md).

## Build (ufbt)

Pinned to the **official release** SDK channel (stock firmware) — see [`../CLAUDE.md`](../CLAUDE.md).

```
pip install ufbt
ufbt update                 # release channel (SDK 1.4.3, API 87.1, target f7)
cd zero
python -m ufbt              # build subcensuszero.fap  (dist/subcensuszero.fap)
python -m ufbt lint         # clang-format check
python -m ufbt launch       # deploy over USB, or copy the .fap to the SD /apps/Sub-GHz/
```

If your device runs Unleashed/Momentum, re-pin the SDK to that firmware (subghz symbol
names differ) and re-verify.

## Generated files (do not hand-edit)

`census_taxonomy.h` and `census_schema.h` are **generated** from `shared/taxonomy.yaml` +
`shared/schema/` (System §10). To change them, edit the source in `shared/` then:

```
python -m subcensus_tools.codegen     # from tools/  (writes the headers)
cd zero && python -m ufbt format      # clang-format the generated output
```

`codegen --check` (content-based) guards against drift; `ufbt lint` guards formatting.

## shared/core in the FAP

`shared/core` is compiled into the FAP as a **unity build** via `subcensus_core.c` (fbt can't
reference `../` sources). It is `float`-only (Cortex-M4F is single-precision). File-scope
`static` names across core `.c` must be unique — shared helpers live in `shared/core/sc_util.h`.

## Layout

```
application.fam        appid subcensuszero, Sub-GHz category
subcensuszero.c        entry, view_dispatcher + scene_manager wiring
subcensuszero_i.h      app state
subcensus_core.c       unity build of shared/core into the FAP
census_storage.{c,h}   settings persistence + Places on-disk model (§4, §5.4, §5.6)
census_freq.{c,h}      frequency presets + allowed-frequency guard (§3.1, §7)
census_taxonomy.h      GENERATED from shared/taxonomy.yaml
census_schema.h        GENERATED from shared/schema/
scenes/                start / settings / places / place actions / text / confirm / about / todo
```

## On-disk layout (SD `/ext`)

```
/ext/apps_data/subcensuszero/
  config.settings                 # global settings + active place
  signatures/                     # GLOBAL brain (shared with the Pi/Esp)
  places/<place_id>/{place.meta, occupancy.csv, watchlist.csv, census_log.csv, captures/}
```

## Tests (no hardware)

The logic core is host-tested via `python test/core/run_tests.py` (repo root); the pure
allowed-frequency predicate has its own native test (`test_freq_bands.c`). Live RSSI /
capture / TX are on-device validation (`TODO(hw)`), not automated. The Flipper serial-RPC
harness (`tools/debug/flipper_*`) drives on-device UI once a Flipper is attached.

## Status

**All milestones complete (M0–M10):**
- **M0** Phase-0 prior-art + SDK pin (`docs/SubCensusZero_Phase0_Findings.md`) · **M1** skeleton
  (menu, Settings, Places, freq guard, About) · **M2** Camp capture engine (`census_worker`:
  async RX → feature vector → `.sub` + census_log + notify; live view) · **M3** Sweep
  (watchlist-or-preset) · **M4** Recon (`census_recon`: hybrid-grid occupancy → occupancy.csv +
  watchlist.csv) · **M5** auto-classify (`SubGhzReceiver`/environment protocol tagging) ·
  **M6** classification DB (`census_brain`: gated k-NN + confirm-appends-fingerprint) ·
  **M7** Dual OOK/FSK (`fsk_suspected`) · **M8** Review + on-device labeling + replay-to-identify ·
  **M9** host `export_place`/`analyze_place` accept a Zero place folder (`tools/`) ·
  **M10** confirm-gated edit-before-transmit TX flow (optional).

Live RSSI / capture / TX are on-device (`TODO(hw)`); the processing they feed is `shared/core`
(native-tested) and every milestone compile-checks under `ufbt`. The full on-device raw-bit /
structured-field editor (M10) is the remaining optional UI depth — its passive primitives
(`sc_diff` / `sc_crc`) already ship. A distributable brain seed lives in
[`../shared/signatures/`](../shared/signatures/).
