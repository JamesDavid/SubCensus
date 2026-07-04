# test/fixtures/ — the deterministic stand-in for a radio (Debug §1.2)

Recorded + synthetic captures that make the decode/classify/log path testable **without
airtime**. Nothing here touches a radio — fixtures prove the *processing*; a human with an
antenna proves the *physics* (the RF boundary, CLAUDE.md / Debug §7).

## Contents

```
sub/                     Flipper .sub RAW captures (Zero/Esp)
  synth_ook_remote_433.sub    PT2262-style OOK remote, 5 repeated frames (repeat-count/preamble)
  synth_weather_ook_433.sub   weather-style OOK beacon, one reception
generate.py              deterministic generator for the synthetic .sub files
```

Planned as later milestones/sessions need them (Debug §1.2):
- `.cu8` / `.ook` IQ/pulse files and recorded `rtl_433` JSON (Pi session).
- Multi-capture corpora per device (varying payloads + timestamps) for cadence (§7a) and
  differential bitfield analysis (§7b).
- Real device recordings dropped alongside the synthetic ones.

## Golden outputs

Expected classifier/feature/cadence results are asserted in the native tests
(`test/core/test_*.c`). For synthetic fixtures the structure is known exactly, so the tests
carry golden values (e.g. `test_fixtures.c` pins the remote's feature vector). When a
`.golden.*` sidecar file is more convenient it lives next to the fixture.

## Regenerating

```
python test/fixtures/generate.py    # byte-identical, reproducible
```

The generator is committed so fixtures are reproducible and reviewable, not opaque blobs.
`.sub`/`.cu8`/`.ook` are treated as binary in `.gitattributes`.
