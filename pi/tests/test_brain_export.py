"""M7: Pi exports into the shared brain + build_signatures merge (Pi §10a, System §6/§8)."""

import sys
from pathlib import Path

import pytest

from subcensuspi.brain_export import (
    export_fingerprints_from_unknowns,
    export_protocol_map_from_devices,
)
from subcensuspi.collector.collector import Collector
from subcensuspi.collector.rtl433 import replay_file
from subcensuspi.db import Database, device_id_for

from subcensus_tools import brain
from subcensus_tools.schema import load_all_schemas, validate_csv
from subcensus_tools.taxonomy import Taxonomy

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import build_signatures  # noqa: E402


@pytest.fixture
def db(tmp_path, fixtures_dir):
    d = Database(tmp_path / "census.db")
    c = Collector(d, place="home", capture_unknowns=True)
    c.process_stream(replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl"))
    return d


def test_protocol_map_export_from_labeled_devices(db):
    did = device_id_for("Acurite-Tower", "1234", "A")
    db.set_device_label(did, label="Garden temp", device_class="weather")
    rows = export_protocol_map_from_devices(db)
    assert any(r["protocol"] == "Acurite-Tower" and r["device_class"] == "weather" for r in rows)
    # unlabeled devices are not exported
    assert all(r["device_class"] for r in rows)


def test_fingerprint_export_from_labeled_unknown(db, tmp_path):
    # the fixture's unknown pulse line has no timings; inject one WITH timings so it carries a fv
    db.insert_unknown(
        ts="2026-07-04T12:00:40", place="home", freq_hz=433920000, source="replay",
        pulse_summary=__import__("json").dumps({
            "fv": {"freq_bin": 433920000, "modulation": "OOK", "sym_dur_us": [350, 1050],
                   "n_symbols": 40, "est_bitrate": 2857, "preamble_len": 8, "repeat_count": 5}
        }),
    )
    uid = db.list_unknowns()[0]["id"]
    db.set_unknown_label(uid, device_class="remote", label="Porch remote")

    rows = export_fingerprints_from_unknowns(db)
    assert len(rows) == 1
    fp = rows[0]
    assert fp["freq_bin"] == 433920000 and fp["device_class"] == "remote"
    assert fp["sym_dur_us_1"] == 350 and fp["source"] == "user"

    # write via the shared brain writer -> validates against the shared schema
    path = tmp_path / "pi_fingerprints.csv"
    brain.write_fingerprints(rows, path)
    assert validate_csv(load_all_schemas()["fingerprints"], path, Taxonomy.load()) == []


def test_device_cadence_persisted_to_catalog(db):
    """A4: dropout-robust cadence from a device's events history lands on the devices row."""
    did = device_id_for("Acurite-Tower", "1234", "A")  # 4 receptions, 60 s apart in the fixture
    est = db.update_device_cadence(did)
    assert est.cls in ("periodic", "quasi-periodic")
    assert abs(est.period_s - 60) <= 2
    dev = db.get_device(did)
    assert dev["cadence_class"] == est.cls
    assert abs(dev["period_s"] - 60) <= 2
    assert dev["cadence_samples"] == est.samples == 3
    # a device seen once -> seen-once, null period
    solo = device_id_for("Schrader", "9ABCDE", "")
    solo_est = db.update_device_cadence(solo)
    assert solo_est.cls == "seen-once"
    assert db.get_device(solo)["period_s"] is None


def test_fingerprint_cadence_from_unknown_reception_history(db):
    """A4: the fingerprint's cadence_* soft features come from the unknown signal's own
    repeated captures at a (place, freq) — the Pi is the strongest measurer (§5/§7a)."""
    import json as _json
    fv = {"fv": {"freq_bin": 868000000, "modulation": "OOK", "sym_dur_us": [200, 600],
                 "n_symbols": 24, "est_bitrate": 5000, "preamble_len": 4, "repeat_count": 3}}
    # five captures of the SAME unknown burst, 60 s apart
    for k in range(5):
        db.insert_unknown(ts=f"2026-07-04T12:{k:02d}:00", place="home", freq_hz=868000000,
                          source="hop", pulse_summary=_json.dumps(fv))
    for u in db.list_unknowns():
        if u["freq_hz"] == 868000000:
            db.set_unknown_label(u["id"], device_class="remote", label="Doorbell")

    rows = [r for r in export_fingerprints_from_unknowns(db) if r["freq_bin"] == 868000000]
    assert rows
    fp = rows[0]
    assert fp["cadence_class"] in ("periodic", "quasi-periodic")
    assert abs(fp["period_s"] - 60) <= 2
    assert fp["cadence_samples"] == 4  # 5 receptions -> 4 intervals


def test_brain_export_cli_writes_pi_exports(db, tmp_path):
    """A8: production CLI emits pi_export/{fingerprints,protocol_map}.csv via the shared writers."""
    from subcensuspi import brain_export

    did = device_id_for("Acurite-Tower", "1234", "A")
    db.set_device_label(did, device_class="weather")
    db.insert_unknown(
        ts="2026-07-04T12:00:40", place="home", freq_hz=433920000, source="replay",
        pulse_summary=__import__("json").dumps({
            "fv": {"freq_bin": 433920000, "modulation": "OOK", "sym_dur_us": [350, 1050],
                   "n_symbols": 40, "est_bitrate": 2857, "preamble_len": 8, "repeat_count": 5}}))
    uid = db.list_unknowns()[0]["id"]
    db.set_unknown_label(uid, device_class="remote")
    db.close()

    cfg = tmp_path / "config.yaml"
    cfg.write_text(f"place: home\ndb_path: {(tmp_path / 'census.db').as_posix()}\n",
                   encoding="utf-8")
    out = tmp_path / "pi_export"
    rc = brain_export.main(["--config", str(cfg), "--out", str(out)])
    assert rc == 0

    tax = Taxonomy.load()
    schemas = load_all_schemas()
    assert (out / "fingerprints.csv").exists() and (out / "protocol_map.csv").exists()
    assert validate_csv(schemas["fingerprints"], out / "fingerprints.csv", tax) == []
    assert validate_csv(schemas["protocol_map"], out / "protocol_map.csv", tax) == []
    pm = brain.read_protocol_map(out / "protocol_map.csv")
    assert brain.lookup_protocol(pm, "Acurite-Tower")["device_class"] == "weather"


def test_pi_export_flows_into_build_signatures(db, tmp_path):
    did = device_id_for("Acurite-Tower", "1234", "A")
    db.set_device_label(did, device_class="weather")
    db.insert_unknown(
        ts="2026-07-04T12:00:40", place="home", freq_hz=433920000, source="replay",
        pulse_summary=__import__("json").dumps({
            "fv": {"freq_bin": 433920000, "modulation": "OOK", "sym_dur_us": [350, 1050],
                   "n_symbols": 40, "est_bitrate": 2857, "preamble_len": 8, "repeat_count": 5}
        }),
    )
    uid = db.list_unknowns()[0]["id"]
    db.set_unknown_label(uid, device_class="remote")

    pi_fp = tmp_path / "pi_fp.csv"
    pi_pm = tmp_path / "pi_pm.csv"
    brain.write_fingerprints(export_fingerprints_from_unknowns(db), pi_fp)
    brain.write_protocol_map(export_protocol_map_from_devices(db), pi_pm)

    sig = tmp_path / "signatures"
    rc = build_signatures.main([
        "--signatures-dir", str(sig),
        "--fingerprints", str(pi_fp),
        "--protocol-map", str(pi_pm),
    ])
    assert rc == 0
    tax = Taxonomy.load()
    schemas = load_all_schemas()
    assert validate_csv(schemas["fingerprints"], sig / "fingerprints.csv", tax) == []
    assert validate_csv(schemas["protocol_map"], sig / "protocol_map.csv", tax) == []
    pm = brain.read_protocol_map(sig / "protocol_map.csv")
    # Pi's device label AND the curated seed both present
    assert brain.lookup_protocol(pm, "Acurite-Tower")["device_class"] == "weather"
    assert brain.lookup_protocol(pm, "Princeton") is not None
