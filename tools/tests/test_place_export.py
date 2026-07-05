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


# --- S-A2: device roll-up parity (period_regularity + match candidate + decoded IDs) ---

MATCH_LOG = (
    "ts_iso,freq_hz,rssi_dbm,duration_ms,preset,fsk_suspected,protocol,key,match_name,"
    "match_class,match_conf,match_source,sub_file,label\n"
    "2026-07-04T12:00:00,433920000,-60.0,0,OOK650,0,Acurite-Tower,ID42,Acurite tower,weather,0.91,decoder,captures/a.sub,\n"
    "2026-07-04T12:01:00,433920000,-61.0,0,OOK650,0,Acurite-Tower,ID42,Acurite tower,weather,0.95,decoder,captures/b.sub,\n"
    "2026-07-04T12:02:00,433920000,-60.0,0,OOK650,0,Acurite-Tower,ID42,,,0.00,,captures/c.sub,\n"
)


def test_device_rollup_regularity_and_match_candidate(tmp_path):
    d = tmp_path / "home_2b3c"
    d.mkdir()
    (d / "census_log.csv").write_text(MATCH_LOG, encoding="utf-8", newline="")
    bundle = build_bundle(d)
    acurite = next(e for e in bundle["devices"]["identified"] if e["protocol"] == "Acurite-Tower")
    # (a) coarse cadence now emits period_regularity (System §7a: 1 - min(1, CoV))
    assert acurite["cadence"]["period_regularity"] == 1.0  # metronomic 60 s
    # (b) match candidate (highest-conf row) + decoded id folded in (parity with the Pi sibling)
    assert acurite["decoded_id"] == "ID42"
    assert acurite["match_candidate"]["device_class"] == "weather"
    assert acurite["match_candidate"]["match_conf"] == 0.95
    assert acurite["match_candidate"]["match_source"] == "decoder"


# --- S-A3: §7b differential + checksum guess folded into the unknowns payload ---

def _sub_from_hex(hexstr: str, freq: int = 315000000, unit: int = 400) -> str:
    bits = "".join(f"{b:08b}" for b in bytes.fromhex(hexstr))
    raw = " ".join(str(unit if c == "1" else -unit) for c in bits)
    return ("Filetype: Flipper SubGhz RAW File\nVersion: 1\n"
            f"Frequency: {freq}\nPreset: FuriHalSubGhzPreset2FSKDev238Async\n"
            f"Protocol: RAW\nRAW_Data: {raw}\n")


def test_unknown_field_discovery_from_captures(tmp_path):
    d = tmp_path / "home_3c4d"
    (d / "captures").mkdir(parents=True)
    # 3 aligned frames with a trailing XOR check byte (byte3 = byte0^byte1^byte2)
    for name, h in [("d.sub", "a50014b1"), ("e.sub", "a50114b0"), ("f.sub", "a50215b2")]:
        (d / "captures" / name).write_text(_sub_from_hex(h), encoding="utf-8")
    log = (
        "ts_iso,freq_hz,rssi_dbm,duration_ms,preset,fsk_suspected,protocol,key,match_name,"
        "match_class,match_conf,match_source,sub_file,label\n"
        "2026-07-04T12:03:00,315000000,-70.0,0,2FSK,1,,,,,0.00,,captures/d.sub,\n"
        "2026-07-04T12:04:00,315000000,-70.0,0,2FSK,1,,,,,0.00,,captures/e.sub,\n"
        "2026-07-04T12:05:00,315000000,-70.0,0,2FSK,1,,,,,0.00,,captures/f.sub,\n"
    )
    (d / "census_log.csv").write_text(log, encoding="utf-8", newline="")
    bundle = build_bundle(d)
    unk = bundle["unknowns"][0]
    fd = unk["field_discovery"]
    assert fd["n_frames"] == 3 and fd["n_bytes"] == 4
    assert [s["class"] for s in fd["segments"]] == ["static", "counter", "slow", "checksum"]
    assert fd["checksum"] is not None and fd["checksum"]["kind"] == 1  # CK_XOR


# --- S-A5: occupancy coverage gaps ---

def test_occupancy_coverage_gaps(zero_place):
    bundle = build_bundle(zero_place)
    gaps = bundle["occupancy_digest"]["coverage_gaps"]
    # 315 MHz and 433.92 MHz are active (>118 MHz apart) -> an unmonitored span
    assert any(g["type"] == "unmonitored-span" for g in gaps)
    # 868/915 have no occupancy -> band-no-occupancy gaps
    bands = {g.get("band") for g in gaps if g["type"] == "band-no-occupancy"}
    assert {"868 MHz", "915 MHz"} <= bands


# --- S-A1: --apply write-back into the global brain ---

def test_apply_labels_writes_protocol_map(tmp_path):
    sig = tmp_path / "signatures"
    analysis = {"identifications": [
        {"signature": "Acurite-Tower", "candidate": "weather", "confidence": 0.9},
        {"signature": "Guessy", "candidate": "tpms", "confidence": 0.5},        # below floor
        {"signature": "Bogus", "candidate": "not-a-class", "confidence": 0.99},  # off-taxonomy
    ]}
    applied = analyze_place.apply_labels(analysis, sig, 0.8, valid_classes={"weather", "tpms"})
    assert [(a["protocol"], a["device_class"]) for a in applied] == [("Acurite-Tower", "weather")]
    rows = brain.read_protocol_map(sig / "protocol_map.csv")
    assert [(r["protocol"], r["device_class"]) for r in rows] == [("Acurite-Tower", "weather")]
    assert "source=user" in rows[0]["notes"]


def test_apply_labels_cli_flag(zero_place, tmp_path):
    # export -> analyze (fake model) -> apply via the CLI (no network)
    out = tmp_path / "out"
    export_place.main(["--place", str(zero_place), "--out", str(out)])
    analysis = {"identifications": [{"signature": "Acurite-Tower", "candidate": "weather", "confidence": 0.95}],
                "field_maps": [], "anomalies": [], "coverage_gaps": [], "inventory": [],
                "recommended_actions": []}
    analyze_place.write_analysis(out, analysis)
    sig = tmp_path / "sig"
    applied = analyze_place.apply_labels(analysis, sig, 0.8, valid_classes=set(_tax_ids()))
    assert applied and (sig / "protocol_map.csv").exists()


def _tax_ids():
    from subcensus_tools.taxonomy import Taxonomy
    return Taxonomy.load().ids()


# --- S-A4: re-runnable, diffs vs the prior analysis ---

def test_diff_vs_prior_analysis(tmp_path):
    prev = {"identifications": [{"signature": "X", "candidate": "weather", "confidence": 0.9}],
            "anomalies": ["old anomaly"]}
    curr = {"identifications": [{"signature": "X", "candidate": "remote", "confidence": 0.9},
                                {"signature": "Y", "candidate": "tpms", "confidence": 0.8}],
            "anomalies": ["new anomaly"]}
    d = analyze_place.diff_analyses(prev, curr)
    assert [i["signature"] for i in d["identifications"]["added"]] == ["Y"]
    assert [c["signature"] for c in d["identifications"]["changed"]] == ["X"]
    assert d["anomalies"]["added"] == ["new anomaly"]
    assert d["anomalies"]["removed"] == ["old anomaly"]

    # write_analysis on a re-run renders the diff into analysis.md
    out = tmp_path / "place"
    analyze_place.write_analysis(out, prev)
    analyze_place.write_analysis(out, curr)
    md = (out / "analysis.md").read_text(encoding="utf-8")
    assert "Diff vs prior analysis" in md
    assert "identifications changed" in md
