"""M9: shared export_place / analyze_place accept a Zero CSV place folder (System §8)."""

import json
import sys
from pathlib import Path

import pytest

from subcensus_tools import brain
from subcensus_tools.place_bundle import build_bundle, render_prompt

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import analyze_place  # noqa: E402
import export_place  # noqa: E402
import build_signatures  # noqa: E402

CENSUS_LOG = (
    "ts_iso,freq_hz,rssi_dbm,duration_ms,preset,fsk_suspected,protocol,key,match_name,"
    "match_class,match_conf,match_source,sub_file,label\n"
    "2026-07-04T12:00:00,433920000,-60.5,0,OOK650,0,Acurite-Tower,,,,0.00,,captures/a.sub,\n"
    "2026-07-04T12:01:00,433920000,-61.0,0,OOK650,0,Acurite-Tower,,,,0.00,,captures/b.sub,\n"
    "2026-07-04T12:02:00,433920000,-60.0,0,OOK650,0,Acurite-Tower,,,,0.00,,captures/c.sub,\n"
    "2026-07-04T12:03:00,315000000,-70.0,0,2FSK,1,,,,,0.00,,captures/d.sub,\n"
)
OCCUPANCY = (
    "freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen\n"
    "433920000,-97.0,-60.0,0.80,9,2026-07-04T12:03:00\n"
    "315000000,-98.0,-70.0,0.20,2,2026-07-04T12:03:00\n"
)


@pytest.fixture
def zero_place(tmp_path):
    d = tmp_path / "home_1a2b"
    d.mkdir()
    (d / "census_log.csv").write_text(CENSUS_LOG, encoding="utf-8", newline="")
    (d / "occupancy.csv").write_text(OCCUPANCY, encoding="utf-8", newline="")
    return d


def test_bundle_from_zero_place(zero_place):
    pm = brain.merge_protocol_map([build_signatures.SEED_PROTOCOL_MAP])
    bundle = build_bundle(zero_place, protocol_map=pm)
    assert bundle["manifest"]["capture_count"] == 4
    # the Acurite (3 captures, 60 s apart) is Identified with a coarse periodic cadence
    ids = bundle["devices"]["identified"]
    acurite = next(e for e in ids if e["protocol"] == "Acurite-Tower")
    assert acurite["count"] == 3
    assert acurite["cadence"]["cadence_class"] in ("periodic", "quasi-periodic")
    assert abs(acurite["cadence"]["period_s"] - 60) <= 2
    # the FSK-suspected 315 capture is a needs-ID unknown
    assert bundle["unknowns"] and bundle["unknowns"][0]["fsk_suspected"]
    # occupancy digest + reference grounding present
    assert bundle["occupancy_digest"]["top_bins"][0]["freq_hz"] == 433920000
    assert "433.92 MHz" in bundle["reference_grounding"]["ism_bands"]
    # protocol_map slice is carried (the seeded brain)
    assert any(r["protocol"] == "Acurite-Tower" for r in bundle["reference_grounding"]["protocol_map_slice"])


def test_export_place_cli_writes_files(zero_place, tmp_path):
    out = tmp_path / "out"
    rc = export_place.main(["--place", str(zero_place), "--out", str(out)])
    assert rc == 0
    assert (out / "bundle.json").exists()
    assert "RF/ISM analyst" in (out / "prompt.md").read_text(encoding="utf-8")
    assert json.loads((out / "bundle.json").read_text(encoding="utf-8"))["manifest"]["capture_count"] == 4


def test_analyze_round_trip_with_fake_model(zero_place, tmp_path):
    bundle = build_bundle(zero_place)

    def fake_model(messages):
        assert any("RF/ISM analyst" in m["content"] for m in messages)
        return "```json\n" + json.dumps({
            "identifications": [
                {"signature": "Acurite-Tower", "candidate": "weather", "confidence": 0.9},
                {"signature": "315MHz-FSK", "candidate": "tpms", "confidence": 0.5},
            ],
            "coverage_gaps": ["no 915 MHz coverage"],
        }) + "\n```"

    analysis = analyze_place.analyze_bundle(bundle, fake_model)
    assert analysis["field_maps"] == []  # defaulted
    proposed = analyze_place.proposed_labels(analysis, 0.8)
    assert len(proposed) == 1 and proposed[0]["candidate"] == "weather"
    out = analyze_place.write_analysis(tmp_path / "a", analysis)
    assert (out / "analysis.json").exists() and (out / "analysis.md").exists()
