# SubCensus

A three-sensor census of ISM-band radio activity around a home. One shared
**data + intelligence layer**; three platform sensors that differ only where the
hardware demands it.

- **SubCensusZero** — portable Flipper Zero FAP (narrowband CC1101; walk-around; on-device
  catalog + review; can replay-to-identify). Build: [`zero/README.md`](./zero/README.md).
- **SubCensusPi** — stationary RTL-SDR / Raspberry Pi service (wideband `rtl_433`; web
  dashboard; RX-only). Build: [`pi/README.md`](./pi/README.md).
- **SubCensusEsp** — headless ESP32 + CC1101 node (same capture model as the Zero; served
  over WiFi). Build: [`esp/README.md`](./esp/README.md).

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
- **SubCensusZero** — M1 (skeleton FAP: menu, settings, Places, freq guard). M2–M10 (capture/
  sweep/recon/classify/replay) pending — the radio-heavy half.
- **SubCensusPi** — complete: M0–M9 (collector → SQLite, dashboard, multi-dongle, unknowns,
  MQTT/HA, occupancy pass, shared brain, Places, field-map discovery). 51 tests green.
- **SubCensusEsp** — in progress.
