# SubCensus

A three-sensor census of ISM-band radio activity around a home. One shared
**data + intelligence layer**; three platform sensors that differ only where the
hardware demands it.

- **SubCensusZero** — portable Flipper Zero FAP (narrowband CC1101; walk-around; on-device
  catalog + review; can replay-to-identify). Build: [`zero/README.md`](./zero/README.md).
- **SubCensusPi** — stationary RTL-SDR / Raspberry Pi service (wideband `rtl_433`; web
  dashboard; RX-only). Build: [`pi/README.md`](./pi/README.md).
- **SubCensusEsp** — headless ESP32 + CC1101 node (same capture model as the Zero; served
  over WiFi). Build: [`esp/README.md`](./esp/README.md) · **flash from your browser** (no
  toolchain) with the [web flasher](./esp/flasher/) → GitHub Pages.

They share the whole **data + intelligence layer** and differ only in capture/UI/build.
The rule of thumb (System §3): **unify the brain and bookkeeping; keep the senses and
controls native.**

## Authoritative contract

`docs/SubCensus_System.md` defines everything shared — Places, the label taxonomy, the
signature DB / classification brain, the feature-vector + cadence schema, the host-side
tools, and the shared data artifacts. **On any conflict, System.md wins.** See
[`CLAUDE.md`](./CLAUDE.md) for the repo invariants and `docs/SubCensus_Debug.md` for the
test harness / build order.

## Layout

```
shared/          single source of truth (taxonomy, schema, C logic core)
  taxonomy.yaml  device_class vocabulary (System §5) -> generates census_taxonomy.h
  schema/        column specs for the shared CSVs (System §7, §9) -> census_schema.h
  core/          host-compilable C logic; compiles into the Zero FAP AND the ESP firmware
                 (feature vector, cadence, gated k-NN, .sub/pulse en·decode, CRC, differential)
tools/           host-side Python used by every sensor (System §8)
  subcensus_tools/  taxonomy/schema loaders, codegen, brain IO, Flipper serial-RPC harness
  build_signatures.py  the single merge point for the global signatures/ brain
test/            fixtures (.sub / rtl_433 JSON / rtl_power CSV) + native core unit tests
zero/            SubCensusZero — Flipper FAP (C, ufbt)
pi/              SubCensusPi — RTL-SDR / Raspberry Pi (Python: collector, dashboard, dsp/)
esp/             SubCensusEsp — ESP32 + CC1101 node (PlatformIO / Arduino)
docs/            the five specs + Phase 0 findings
```

`shared/` is the single source of truth; derived artifacts are **generated, not
hand-maintained** (System §10), so the sensors can't drift.

## The RF boundary (applies to every target)

No tool emulates the CC1101 or RTL-SDR. Recorded **fixtures** make the entire *processing*
path deterministic and testable off-device; a **human with an antenna** proves the
*physics* (a real signal was received / a real device reacted to a replay). Live RSSI /
capture / TX are on-device validation steps, not automated tests. **Monitoring is passive**
— Sweep/Camp/Recon never transmit; Replay/Edit-TX (Zero, Esp) is the only TX path, explicit
and TX-allow-list gated.

## Using the sensors

Each target has a **screen/page-by-screen usage walkthrough** in its own README:

- **SubCensusZero** — [`zero/README.md`](./zero/README.md) → *"Using it — screen by screen"*:
  every FAP scene (Recon → Sweep/Camp → Review → Edit) with a mockup of each screen.
- **SubCensusPi** — [`pi/README.md`](./pi/README.md) → *"Using the dashboard"*:
  the web dashboard (Devices + sparklines, Live feed, Unknowns, Bands).
- **SubCensusEsp** — [`esp/README.md`](./esp/README.md) → *"Web UI + API"*:
  the tabbed web UI (Live, Review, Bands, Field-map, Places, Settings).

<p align="center">
  <img src="./zero/docs/screens/05_recon_spectrum.svg" width="300" alt="Zero Recon spectrum strip">
  <img src="./zero/docs/screens/11_camp_live.svg" width="300" alt="Zero Camp live view">
</p>

> The Flipper screens in `zero/docs/screens/` are **placeholder mockups** modelled from the draw
> code, to be swapped for real qFlipper captures once the FAP runs on hardware.

## Host-side tests (no hardware)

```
# shared C logic core — native, via zig cc (pip install ziglang)
python test/core/run_tests.py

# host Python tools (codegen, schema, brain, serial harness)
pip install -e tools/[dev] && python -m pytest tools/

# regenerate the on-device artifacts from shared/ (System §10)
python -m subcensus_tools.codegen        # then `ufbt format` in zero/
```

Per-target build + test instructions live in each target's README (linked above).

## Status

- **Shared layer** — complete: taxonomy + schema + codegen, `shared/core` (10 native test
  files), host tools + brain, fixtures.
- **SubCensusZero** — **complete (M0–M10), spec-delta zero**: Phase-0, skeleton, full §4 Settings,
  Camp/Sweep/Recon capture, auto-following Recon spectrum strip, Recon-results Pin/Exclude/Camp-
  here + Reset, auto-classify, classification DB (k-NN + confirm-appends-fingerprint), real Dual
  OOK→FSK re-capture, Review + in-place labeling + confirm-gated replay, Camp picker + custom
  freq editor, SD-required/full states, and the full M10 edit-before-transmit / field-map
  discovery editor (raw/structured/differential + decode-back gate + propose `field_maps/`). `ufbt`
  build + lint clean; live radio `TODO(hw)`.
- **SubCensusPi** — **complete (M0–M9)**: collector → SQLite (MQTT/HA wired in), dashboard
  (device sparklines, unknowns inspect/IQ, Bands **occupancy heatmap + sweep waterfall** + pin/
  exclude + recon controls), multi-dongle + rtl_433 relaunch supervision + watchlist attention
  priority, occupancy pass, shared brain + cadence export, systemd units, Places, field-map
  discovery. **85 tests green**.
- **SubCensusEsp** — **complete (M1–M8)**: skeleton, RMT capture + Camp, Recon/Sweep (accumulate/
  fresh + pin preservation), classification with a per-device running **cadence estimator**, full
  web UI incl. Bands pin/exclude/camp-here, Review top-N candidates + taxonomy picker, **field-map
  discovery overlay** (differential + segment labeling + checksum re-sign + guarded own-device
  edit-TX → proposed `field_maps/`), per-identified-device HA discovery, CC1101 preset register
  tables, SD auto-detect, MQTT/HA + brain sync + OTA, replay/edit-TX. 13 native + 12 web-driver
  tests; `pio run` clean (80% flash). **[Browser web flasher](https://jamesdavid.github.io/SubCensus/)**.
- **Shared layer** — `shared/core` now also carries `sc_fieldmap` (field-map + checksum re-sign +
  `.fmap` IO) and `sc_slice` (RAW↔bit-frame), consumed identically by the Zero and Esp editors;
  `build_signatures.py --places` proposes `field_maps/` entries from a place's capture corpus.
- **Brain seed** — [`shared/signatures/`](./shared/signatures/): distributable `protocol_map.csv`
  (~64 Flipper + rtl_433 protocols) so a fresh install classifies out of the box.
