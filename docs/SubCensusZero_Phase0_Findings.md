# SubCensusZero — Phase 0 findings (prior-art → module map + SDK pin)

Deliverable gate for Zero §0 / §9-M0. Produced before any implementation. Maps each
prior-art reference to the SubCensusZero module it informs, and records the firmware/SDK
the build targets. All symbol names below are **subject to the pinned SDK** (§ SDK pin)
and are verified/adjusted at build time, per Zero §5.1.

## Reference → module map

| Reference | Informs (module) | What we reuse / adapt |
|---|---|---|
| **Read RAW** — `applications/main/subghz/scenes/subghz_scene_read_raw.c`, `SubGhzProtocolDecoderRAW`, file-encoder worker | `zero/census_worker.{c,h}` capture path (§5.1); `zero/census_log.{c,h}` `.sub` writer | Async RX → RAW file-encoder → `.sub`, RSSI-gated. Template for M2 Camp. |
| **Frequency Analyzer** — `subghz_frequency_analyzer_worker.c`, `furi_hal_subghz_get_rssi` | Sweep RSSI loop (§3.1); Camp monitor (§3.2) | RSSI sampled across the freq list, peak reported. M3 Sweep core. |
| **Spectrum Analyzer FAP** | Recon Stage A stepped sweep + live spectrum strip (§3.3 / §6) | Stepped RSSI across a segment. SubCensus **aggregates** it into `occupancy.csv` + watchlist, not just draws it. M4. |
| **Read** — `SubGhzReceiver` + `SubGhzEnvironment` protocol registry | Auto-classify (§5.1 step 2), M5 | Opportunistic known-protocol decode/tag during capture. |
| **ProtoView** (antirez) | `shared/core/` decode helpers; feature vector (§7); OOK/2-FSK/TPMS handling (§5.3); "unknown" bucket; field-map work (§7b) | Pulse-bitmap model, seek sync/preamble, line-code decode, checksum verify. **GPL — study/reference parameters, do not copy code into the FAP.** |
| **FlipRSDR** (2026) | Capture timing fidelity (§5.1); feature vector (§5.5) | Burst start/continuation/completion from idle-gap; "preserve timing first, decode later." Contrast: FlipRSDR is PC-tethered; SubCensus keeps an on-device catalog + host-batch LLM analysis. |
| **Weather Station** (Skorpionm) / **TPMS** (wosk) | Alt protocol-set loading; continuous 2-FSK decode (§5.3) | Gap we close: they find but **can't save** — SubCensus persists + catalogs. |
| **Subdriving** (Xtreme + TinyGPS) | Places `place.meta` optional location tag (§5.6) | Reference only if an external GPS is ever wired. Not required for v1. |
| **jamisonderek Sub-GHz wiki** / **flipc.org** | `.sub` format for `shared/core/` en·decode + fixtures | Ground-truth `.sub` format; offline verification of synthetic fixtures. |
| **Sub-GHz app source (dev branch)** | Symbol-name ground truth; the SDK pin target | Confirm `subghz_devices_*` abstraction vs legacy `furi_hal_subghz_*` on the pinned firmware. |

## RF boundary (Debug §7)

None of the above emulate the CC1101. `shared/core/` + `test/fixtures/` prove the
*processing* path; live RSSI/capture/TX are human-with-antenna validation. Every test is
labeled for which side of the boundary it exercises.

## Firmware / SDK pin

- **Channel:** `ufbt` **official `release`** (stock Flipper firmware). Matches the repo
  invariant (`CLAUDE.md`).
- **Radio API:** target the `subghz_devices_*` abstraction (internal CC1101 vs external)
  present on current stock firmware; fall back to `furi_hal_subghz_*` only if the pinned
  SDK lacks it.
- **TX gating:** replay/edit-TX checks the firmware TX allow-list
  (`furi_hal_subghz_is_tx_allowed` or the SDK-version equivalent) before any transmit.
- **Verification cadence:** exact subghz symbol names/signatures are confirmed against the
  pinned SDK at each milestone's `ufbt` compile-check; the About scene surfaces the
  built-against API version so a device/FAP mismatch is diagnosable (§6.1).

## Gate status

Phase 0 complete: prior-art studied, reference→module map recorded, firmware/SDK pinned
(release channel). Milestone 1 may begin.
