"""M4: unknown capture + review queue + disk guard (Pi §4, §6)."""

import json

import pytest
from fastapi.testclient import TestClient

from subcensuspi.collector.collector import Collector
from subcensuspi.collector.rtl433 import replay_file
from subcensuspi.collector.unknowns import (
    dir_size_bytes,
    disk_guard_ok,
    pulse_summary_from_event,
)
from subcensuspi.db import Database
from subcensuspi.web.app import create_app


@pytest.fixture
def db(tmp_path):
    d = Database(tmp_path / "census.db")
    yield d
    d.close()


def test_disk_guard(tmp_path):
    iq = tmp_path / "iq"
    iq.mkdir()
    assert disk_guard_ok(iq, 20) is True
    (iq / "snip.cu8").write_bytes(b"\x00" * 2048)
    assert dir_size_bytes(iq) == 2048
    # a tiny cap (2 KB worth of GB) is already exceeded
    assert disk_guard_ok(iq, 2048 / (1024**3)) is False
    assert disk_guard_ok(iq, 0) is True  # 0 => unlimited


def test_pulse_summary_with_timings():
    obj = {"freq": 433.92, "mod": "ASK", "pulses": 20,
           "timings": [350, -350, 350, -350, 1050, -350, 350, -1050]}
    summary = json.loads(pulse_summary_from_event(obj))
    assert summary["n_pulses"] == 20
    assert summary["mod"] == "ASK"
    assert summary["sym_widths_us"]  # dominant widths extracted via shared DSP
    assert summary["est_bitrate"] > 0


def test_collector_captures_unknowns(db, fixtures_dir):
    c = Collector(db, place="home", capture_unknowns=True)
    c.process_stream(replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl"))
    assert db.unknown_count() == 1
    assert c.stats.unknowns == 1
    row = db.list_unknowns()[0]
    assert row["place"] == "home"
    assert row["freq_hz"] == 433920000
    assert row["pulse_summary"] is not None


def test_disk_guard_blocks_capture(db, fixtures_dir, tmp_path):
    iq = tmp_path / "iq"
    iq.mkdir()
    (iq / "big.cu8").write_bytes(b"\x00" * 4096)
    c = Collector(db, place="home", capture_unknowns=True,
                  iq_dir=str(iq), max_iq_gb=4096 / (1024**3))  # already over
    c.process_stream(replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl"))
    assert db.unknown_count() == 0
    assert c.stats.unknowns_dropped == 1


def test_review_queue_api(db, fixtures_dir, tmp_path):
    c = Collector(db, place="home", capture_unknowns=True)
    c.process_stream(replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl"))
    db.close()
    client = TestClient(create_app(str(tmp_path / "census.db")))

    unknowns = client.get("/api/unknowns").json()
    assert len(unknowns) == 1
    uid = unknowns[0]["id"]

    # classify
    r = client.post(f"/api/unknown/{uid}/label", data={"device_class": "remote", "notes": "porch"})
    assert r.status_code == 200
    # invalid class rejected
    assert client.post(f"/api/unknown/{uid}/label", data={"device_class": "nope"}).status_code == 400
    # discard
    assert client.delete(f"/api/unknown/{uid}").status_code == 200
    assert client.get("/api/unknowns").json() == []
    assert client.delete(f"/api/unknown/{uid}").status_code == 404
