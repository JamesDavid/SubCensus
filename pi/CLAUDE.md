# pi/CLAUDE.md — SubCensusPi notes

SubCensusPi is the stationary RTL-SDR / Raspberry Pi counterpart to the Zero. It shares the
whole data + intelligence layer (`SubCensus_System.md` is authoritative) and keeps only its
capture model, web UI, RX-only nature, and `rtl_433`-native modulation handling platform-specific.

## Invariants (inherit repo `../CLAUDE.md`)
- **System.md is authoritative.** Do not modify the shared contract (`shared/taxonomy.yaml`,
  `shared/schema/`, `shared/core/`, the signatures/place/catalog definitions). Reuse them.
- **RF boundary:** fixtures prove *processing*; a real dongle proves *physics*. Only live
  gain/ppm/reception needs hardware — everything downstream of a captured sample is
  fixture-testable (Debug §4). Mark live capture `TODO(hw)`.
- **RX-only.** No TX/replay (that's Zero/Esp). Passive analysis + labeling only.

## Cross-tool parity (the important bit)
`subcensuspi/dsp/` is the **Python reimplementation of `shared/core`** (feature vector §7,
cadence §7a, gated k-NN §6, `.sub`/pulse en·decode, CRC + differential §7b, occupancy §9).
It is **parity-locked to the C core**: `pi/tests/test_dsp_parity.py` loads the SAME
`test/fixtures/` and asserts the SAME golden values the C tests assert. If the C core changes,
update the Python port + parity test in lockstep. This is what keeps a Zero place and a Pi
place interchangeable (System §7 binding).

## No-hardware test path (Debug §4)
- rtl_433 may be absent (Windows dev): the collector is driven by **recorded rtl_433 JSON**
  fixtures (`feed recorded rtl_433 JSON straight to the collector`, Debug §4). The
  `rtl_433 -r <fixture.cu8>` path is implemented but skipped when the binary is absent.
- `pytest` for collector / catalog / cadence / SQLite; **httpx** for the FastAPI dashboard
  (Playwright optional). MQTT/HA discovery tested via a **fake broker** (payload assertions).

## Build / run (Linux target)
`pip install -e .[dev]` (from `pi/`). **ONE** systemd service in production (§9):
`subcensuspi` (the uvicorn dashboard). The dashboard OWNS the single dongle via `radio.py`'s
`RadioManager` and switches it between mutually-exclusive modes — **off / decode / spectrum** —
from the Capture control; `rtl_433` (decode) and `rtl_power` (spectrum) can't share one radio, so
there is no separate collector service. Decode mode launches the collector CLI
(`python -m subcensuspi.collector.main --config …`) as a managed subprocess; that CLI is also the
no-hardware replay path for tests/CI. Locally: `uvicorn subcensuspi.web.app:app` (set
`SUBCENSUSPI_CONFIG`/`SUBCENSUSPI_RADIO_STATE` for decode + boot-resume).
