# SubCensusPi — RTL-SDR / Raspberry Pi

The stationary, run-it-for-a-week counterpart to the Zero. Wideband: decodes hundreds of ISM
device protocols via `rtl_433`, logs everything continuously to SQLite, captures the
undecodable for later classification, and surfaces it in a small FastAPI dashboard with
MQTT → Home Assistant discovery. **RX-only.** Spec:
[`../docs/SubCensusPi_Spec.md`](../docs/SubCensusPi_Spec.md); shared contract:
[`../docs/SubCensus_System.md`](../docs/SubCensus_System.md).

## Install

```
cd pi
pip install -e .[dev]        # fastapi, uvicorn, jinja2, python-multipart, paho-mqtt, pyyaml (+ pytest, httpx)
# system: rtl-433 (or build from source) + a current librtlsdr (needed for RTL-SDR Blog v4)
```

## Run

```
# collector: rtl_433 -> SQLite  (live needs a dongle)
python -m subcensuspi.collector.main --config config.yaml

# no hardware: drive the full decode -> catalog -> SQLite path from recorded rtl_433 JSON
python -m subcensuspi.collector.main --config config.example.yaml \
    --replay ../test/fixtures/rtl433/home_stream.jsonl --db /tmp/census.db

# dashboard
SUBCENSUSPI_DB=/tmp/census.db uvicorn subcensuspi.web.app:app --host 0.0.0.0 --port 8080
```

Config is `config.example.yaml` (Pi §8): dongles (single-hop or multi-dongle by serial),
`place`, `places_dir`, global `signatures_dir`, `iq_dir` + `max_iq_gb` disk guard, MQTT/HA,
web host/port. Two systemd services in production (Pi §9): `collector` + `web`.

## Architecture

```
subcensuspi/
  dsp/             Python port of shared/core — PARITY-LOCKED to the C golden fixtures
                   (sub, pulse, feature §7, cadence §7a, knn §6, crc + diff §7b, occupancy §9)
  db.py            SQLite catalog (devices/events/unknowns, WAL, place-scoped) — Pi §5
  config.py        YAML config (Pi §8)
  collector/       parser, Collector (fixture-drivable), multi-dongle runner, rtl433 launcher
  web/             FastAPI dashboard + JSON API + WebSocket-less live poll (Pi §7)
  occupancy_pass.py  rtl_power sweep -> shared-schema occupancy.csv/watchlist.csv (Pi §3, §9a)
  mqtt.py          MQTT -> Home Assistant discovery (Pi §9)
  brain_export.py  emit protocol_map/fingerprints into the shared brain (Pi §10a)
  export_place.py  roll a place -> shared analysis bundle + prompt.md (System §8)
  analyze_place.py provider-agnostic structured analysis (default local; System §8)
  fieldmap.py      passive field-map discovery over the events corpus (System §7b) — RX-only
```

## Cross-tool parity (important)

`subcensuspi/dsp/` reimplements `shared/core` in Python and is **parity-locked**:
`tests/test_dsp_parity.py` loads the SAME `test/fixtures/` and asserts the SAME golden values
the C tests assert. Emitted `occupancy.csv`/`watchlist.csv`/`fingerprints.csv` are validated
against the SAME `shared/schema/`. So a Zero place and a Pi place are interchangeable (System §7/§9a).

## Tests (no hardware)

```
cd pi && python -m pytest         # 51 tests: DSP parity, collector, dashboard, multi-dongle,
                                  # unknowns, MQTT, occupancy pass, shared brain, Places, field-map
```

rtl_433 is driven from **recorded JSON** fixtures (no dongle, no rtl_433 binary needed);
`rtl_433 -r <fixture.cu8>` is supported when the binary is present. Dashboard via httpx;
MQTT via a fake broker. Only live dongle behaviour (gain/ppm/reception) and `rtl_power`
sweeps need hardware (`TODO(hw)`).

## Status

**Complete — M0–M9** (all Pi §11 milestones). 51 tests green.
