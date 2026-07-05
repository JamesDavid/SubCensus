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


# --- S-A3: §7b differential + checksum guess folded into the needs-ID payload ---

def test_needs_id_field_discovery(tmp_path, fixtures_dir):
    d = Database(tmp_path / "fm.db")
    Collector(d, place="home").process_stream(
        replay_file(fixtures_dir / "rtl433" / "fieldmap_corpus.jsonl"))
    bundle = build_bundle(d, "home")
    raw = next(e for e in bundle["devices"]["needs_id"] if e["model"] == "RawSensor")
    fd = raw["field_discovery"]
    assert fd["n_frames"] == 8 and fd["n_bytes"] == 4
    assert [s["class"] for s in fd["segments"]] == ["static", "counter", "slow", "checksum"]
    assert fd["checksum"]["kind"] == "xor"


# --- S-A5: occupancy coverage gaps in the digest ---

def test_occupancy_coverage_gaps(db, tmp_path):
    occ = tmp_path / "occupancy.csv"
    occ.write_text(
        "freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen\n"
        "433920000,-97.0,-60.0,0.80,9,2026-07-04T12:03:00\n"
        "315000000,-98.0,-70.0,0.20,2,2026-07-04T12:03:00\n",
        encoding="utf-8", newline="")
    bundle = build_bundle(db, "home", occupancy_csv=occ)
    gaps = bundle["occupancy_digest"]["coverage_gaps"]
    assert any(g["type"] == "unmonitored-span" for g in gaps)
    bands = {g.get("band") for g in gaps if g["type"] == "band-no-occupancy"}
    assert {"868 MHz", "915 MHz"} <= bands


# --- S-A1: --apply write-back into the global brain ---

def test_apply_labels_writes_protocol_map(tmp_path):
    from subcensuspi.analyze_place import apply_labels
    from subcensus_tools.schema import load_all_schemas, validate_csv
    from subcensus_tools.taxonomy import Taxonomy

    sig = tmp_path / "signatures"
    analysis = {"identifications": [
        {"signature": "Acurite-Tower", "candidate": "weather", "confidence": 0.9},
        {"signature": "Guessy", "candidate": "tpms", "confidence": 0.4},         # below floor
        {"signature": "Bogus", "candidate": "not-a-class", "confidence": 0.99},  # off-taxonomy
    ]}
    applied = apply_labels(analysis, sig, 0.8, valid_classes={"weather", "tpms"})
    assert [(a["protocol"], a["device_class"]) for a in applied] == [("Acurite-Tower", "weather")]
    pm = sig / "protocol_map.csv"
    assert pm.exists()
    # the write is schema-valid (shared/schema/protocol_map, System §10)
    errs = validate_csv(load_all_schemas()["protocol_map"], pm, Taxonomy.load())
    assert errs == []


def test_apply_labels_idempotent_and_updates(tmp_path):
    from subcensuspi.analyze_place import apply_labels
    sig = tmp_path / "sig"
    apply_labels({"identifications": [{"signature": "P", "candidate": "weather", "confidence": 0.9}]},
                 sig, 0.8)
    # re-apply with a corrected class -> updates in place, no duplicate rows
    apply_labels({"identifications": [{"signature": "P", "candidate": "remote", "confidence": 0.9}]},
                 sig, 0.8)
    import csv
    with (sig / "protocol_map.csv").open(encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    assert len(rows) == 1 and rows[0]["device_class"] == "remote"


# --- S-A4: re-runnable, diffs vs the prior analysis ---

def test_diff_vs_prior_analysis(tmp_path):
    from subcensuspi.analyze_place import diff_analyses

    prev = {"identifications": [{"signature": "X", "candidate": "weather", "confidence": 0.9}],
            "anomalies": ["old"]}
    curr = {"identifications": [{"signature": "X", "candidate": "remote", "confidence": 0.9},
                                {"signature": "Y", "candidate": "tpms", "confidence": 0.8}],
            "anomalies": ["new"]}
    d = diff_analyses(prev, curr)
    assert [i["signature"] for i in d["identifications"]["added"]] == ["Y"]
    assert [c["signature"] for c in d["identifications"]["changed"]] == ["X"]
    assert d["anomalies"]["added"] == ["new"] and d["anomalies"]["removed"] == ["old"]

    out = tmp_path / "place"
    write_analysis(out, prev)
    write_analysis(out, curr)
    md = (out / "analysis.md").read_text(encoding="utf-8")
    assert "Diff vs prior analysis" in md and "identifications changed" in md
