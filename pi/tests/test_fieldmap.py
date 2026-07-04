"""M9: passive field-map discovery over the events corpus (System §7b). RX-only, no TX."""

import pytest
from fastapi.testclient import TestClient

from subcensuspi.collector.collector import Collector
from subcensuspi.collector.rtl433 import replay_file
from subcensuspi.db import Database, device_id_for
from subcensuspi.dsp import crc
from subcensuspi.fieldmap import (
    analyze_device,
    discover_checksum,
    frames_from_events,
)
from subcensuspi.web.app import create_app


@pytest.fixture
def db(tmp_path, fixtures_dir):
    d = Database(tmp_path / "census.db")
    Collector(d, place="home").process_stream(
        replay_file(fixtures_dir / "rtl433" / "fieldmap_corpus.jsonl")
    )
    return d


DID = device_id_for("RawSensor", "7", "")


def test_frames_reconstructed(db):
    frames = frames_from_events(db, DID)
    assert len(frames) == 8
    assert frames[0] == bytes([0xA5, 0x00, 0x14, 0xB1])


def test_checksum_discovered_across_corpus(db):
    frames = frames_from_events(db, DID)
    spec = discover_checksum(frames)
    assert spec is not None and spec.kind == crc.ChecksumKind.XOR


def test_differential_segments_and_ground_truth(db):
    p = analyze_device(db, DID, ground_truth_field="temperature_C")
    assert p is not None
    assert p.n_frames == 8 and p.n_bytes == 4
    cls = [s.cls for s in p.fields]
    assert cls[0] == "static"    # byte0 = 0xA5 id/address
    assert cls[1] == "counter"   # byte1 = per-frame counter
    assert cls[2] == "slow"      # byte2 = sensor value
    assert cls[3] == "checksum"  # byte3 = XOR check byte
    # ground-truth correlation attaches semantics to the slow byte
    slow = p.fields[2]
    assert slow.semantics == "tracks temperature_C"
    assert slow.correlation is not None and abs(slow.correlation) > 0.95
    assert p.checksum["kind"] == "xor"
    assert p.confidence > 0.5


def test_fieldmap_api(db, tmp_path):
    db.close()
    client = TestClient(create_app(str(tmp_path / "census.db")))
    r = client.get(f"/api/device/{DID}/fieldmap?ground_truth=temperature_C")
    assert r.status_code == 200
    data = r.json()
    assert [f["class"] for f in data["fields"]] == ["static", "counter", "slow", "checksum"]
    assert data["checksum"]["kind"] == "xor"
    assert "PROPOSAL" in data["reasoning"]  # never auto-committed (System §7b)
    # a decoded device with no raw payload -> 422 (not enough for differential)
    other = device_id_for("Nope", "", "")
    assert client.get(f"/api/device/{other}/fieldmap").status_code == 404
