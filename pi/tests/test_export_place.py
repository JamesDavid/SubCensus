"""M8: place-scoped export_place bundle + analyze_place round-trip (System §8)."""

import json
import sys
from pathlib import Path

import pytest

from subcensuspi.analyze_place import analyze_bundle, proposed_labels, write_analysis
from subcensuspi.collector.collector import Collector
from subcensuspi.collector.rtl433 import replay_file
from subcensuspi.db import Database, device_id_for
from subcensuspi.export_place import build_bundle, export_place

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import build_signatures  # noqa: E402


@pytest.fixture
def db(tmp_path, fixtures_dir):
    d = Database(tmp_path / "census.db")
    Collector(d, place="home").process_stream(replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl"))
    return d


def test_bundle_structure_and_cadence(db):
    did = device_id_for("Acurite-Tower", "1234", "A")
    db.set_device_label(did, device_class="weather")
    bundle = build_bundle(db, "home", protocol_map=build_signatures.SEED_PROTOCOL_MAP)

    assert bundle["manifest"]["place"] == "home"
    assert bundle["manifest"]["device_count"] == 4
    ids = bundle["devices"]["identified"]
    acurite = next(e for e in ids if e["model"] == "Acurite-Tower")
    # cadence computed from the 4 receptions (60 s apart)
    assert acurite["cadence"]["cadence_class"] in ("periodic", "quasi-periodic")
    assert abs(acurite["cadence"]["period_s"] - 60) <= 2
    # protocol_map match candidate attached
    assert acurite["match_candidate"]["device_class"] == "weather"
    # unlabeled devices are Needs-ID
    assert {e["model"] for e in bundle["devices"]["needs_id"]} == {"Prologue-TH", "Schrader", "Generic-Remote"}
    # reference grounding present
    assert "433.92 MHz" in bundle["reference_grounding"]["ism_bands"]
    assert bundle["reference_grounding"]["typical_cadences"]["weather"]


def test_export_writes_bundle_and_prompt(db, tmp_path):
    out = export_place(db, "home", tmp_path / "out")
    assert (out / "bundle.json").exists()
    prompt = (out / "prompt.md").read_text(encoding="utf-8")
    assert "RF/ISM analyst" in prompt
    assert json.loads((out / "bundle.json").read_text(encoding="utf-8"))["manifest"]["place"] == "home"


def test_analyze_round_trip_with_fake_model(db, tmp_path):
    bundle = build_bundle(db, "home")

    def fake_model(messages):
        # a model would return structured JSON; wrap it in a fence like a real reply
        return "Here is my analysis:\n```json\n" + json.dumps({
            "inventory": [{"model": "Acurite-Tower"}],
            "identifications": [
                {"signature": "Acurite-Tower", "candidate": "weather", "confidence": 0.9, "reasoning": "433 OOK periodic"},
                {"signature": "Generic-Remote", "candidate": "remote", "confidence": 0.5, "reasoning": "one-shot"},
            ],
            "anomalies": [],
            "coverage_gaps": ["no 915 MHz coverage"],
            "recommended_actions": ["camp 433.92 for one-shots"],
        }) + "\n```\n"

    analysis = analyze_bundle(bundle, fake_model)
    assert analysis["field_maps"] == []  # defaulted
    assert len(analysis["identifications"]) == 2
    # only the >=0.8 identification becomes a proposed label (human-in-the-loop, System §6)
    proposed = proposed_labels(analysis, confidence_floor=0.8)
    assert len(proposed) == 1 and proposed[0]["candidate"] == "weather"

    out = write_analysis(tmp_path / "home", analysis)
    assert (out / "analysis.json").exists() and (out / "analysis.md").exists()
    assert "Coverage Gaps" in (out / "analysis.md").read_text(encoding="utf-8")
