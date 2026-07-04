# CLAUDE.md — SubCensus monorepo invariants

Read this before making changes. These are the load-bearing rules for the whole repo.

## Authority
- **`docs/SubCensus_System.md` is authoritative.** On any conflict between a platform
  spec (Zero/Pi/Esp) and System.md, **System.md wins.** Platform specs cover only
  platform-specific capture/UI/build and defer to System for the shared layer.
- The shared layer (`shared/taxonomy.yaml`, `shared/schema/`, `shared/core/`, the
  signatures/place/catalog contract) is the **single source of truth**. Derived
  artifacts are **generated, not hand-maintained** (System §10): a schema/taxonomy
  change lands in `shared/` and every consumer is regenerated in the same commit, so
  the tools can't drift. Never hand-edit a generated file (e.g. `zero/census_taxonomy.h`,
  generated CSV headers) — edit the source in `shared/` and re-run codegen.

## The RF boundary (hard, applies to every target)
- **Nothing emulates the CC1101 or RTL-SDR.** Automation proves the *processing*
  (decode, classify, cadence, field-map, `.sub` en/decode, CSV/index, UI) via recorded
  **fixtures**; a **human with an antenna** proves the *physics* (a real signal was
  received / a real device reacted to a replay).
- Keep every test honest about which side of that line it sits on. Live RSSI / capture /
  TX are **on-device TODOs**, not blockers — implement fully, cover everything testable
  with fixtures + compile-check, mark on-device validation `TODO(hw)`, and continue.
- **Monitoring is passive** (Sweep/Camp/Recon never transmit). **Replay/Edit-TX is the
  only TX path** — explicit, manual, single-frame, and gated by the firmware TX
  allow-list. Never auto-TX from a scan loop.

## Firmware / SDK pin
- **`ufbt` pinned to the official `release` channel** (stock Flipper firmware) unless the
  maintainer says otherwise. Code against the `subghz_devices_*` abstraction present on
  current stock firmware. If the pin changes (Unleashed/Momentum/rc/dev), re-verify
  subghz symbol names against that firmware's SDK — they differ across builds.
- The About scene surfaces the built-against API version so a mismatch is diagnosable.

## Workflow
- **Commit per milestone** with a milestone-referencing message
  (e.g. `feat(zero): M2 camp capture engine`).
- After each milestone: run the relevant host tests, `ufbt` compile-check + `ufbt lint`
  the FAP where applicable, then commit, then report at the milestone boundary.
- **Never auto-relabel / never auto-commit derived structures** (field-maps, labels).
  The classification brain proposes; the user confirms (System §6, §7b).

## Build order (Debug §6 — front-load hardware-free work)
1. Fixtures + `shared/core/` host unit tests.
2. `tools/debug/flipper_*` serial harness.
3. (Pi test surface — later session.)
4. (Esp — later session.)
5. On-device UI loops (real firmware).
6. RF validation passes (human + antenna).

## Layout
Monorepo per System §2. `shared/` = source of truth; `tools/` = host Python (both tools);
`test/` = fixtures + native core tests; `zero/` = Flipper FAP (self-contained ufbt build).
Docs live in `docs/` at repo root (the five specs + this session's Phase 0 note).
