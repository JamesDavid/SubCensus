# SubCensus

A three-sensor census of ISM-band radio activity around a home. One shared
**data + intelligence layer**; three platform sensors that differ only where the
hardware demands it.

- **SubCensusZero** — portable Flipper Zero FAP (narrowband CC1101; walk-around; on-device
  catalog + review; can replay-to-identify). *This repo's first target.*
- **SubCensusPi** — stationary RTL-SDR / Raspberry Pi service (wideband `rtl_433`; web
  dashboard; RX-only). *Later session.*
- **SubCensusEsp** — headless ESP32 + CC1101 node (same capture model as the Zero; served
  over WiFi). *Later session.*

## Authoritative contract

`docs/SubCensus_System.md` defines everything shared — Places, the label taxonomy, the
signature DB / classification brain, the feature-vector + cadence schema, the host-side
tools, and the shared data artifacts. **On any conflict, System.md wins.** See
[`CLAUDE.md`](./CLAUDE.md) for the repo invariants.

## Layout

```
shared/          single source of truth (taxonomy, schema, C logic core)
  taxonomy.yaml  device_class vocabulary (System §5)
  schema/        column specs for the shared CSVs (System §7, §9)
  core/          host-compilable C logic (feature vector, cadence, k-NN, .sub, CRC)
tools/           host-side Python used by all sensors (System §8)
  debug/         Flipper serial-RPC harness (screenshot / input / logs)
test/            fixtures (.sub / .cu8 / rtl_433 JSON) + native core unit tests
zero/            SubCensusZero Flipper FAP (C, ufbt)
docs/            the five specs + Phase 0 findings
```

## The RF boundary

No tool emulates the CC1101 or RTL-SDR. Recorded **fixtures** make the entire *processing*
path deterministic and testable off-device; a **human with an antenna** proves the
*physics*. Live RSSI / capture / TX are on-device validation steps, not automated tests.

## Building the Zero FAP

```
pip install ufbt
ufbt update              # pinned: official release channel (stock firmware)
ufbt                     # build subcensuszero.fap
ufbt lint
ufbt launch              # deploy over USB (or copy the .fap to the SD card)
```

Host-side core tests (no hardware): see `test/` and `tools/`.
