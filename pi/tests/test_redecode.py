"""Captured-burst re-decode (System §6): replay a saved .cu8 through every decoder and rank the
candidates; prune keeps the sample window bounded; the endpoints list samples and run a re-decode.
No rtl_433 binary — the replay runner is injectable, and the endpoint test monkeypatches it."""

import os

from fastapi.testclient import TestClient

from subcensuspi.collector.rtl433 import prune_samples
from subcensuspi.db import Database, Reception
from subcensuspi.redecode import redecode_file
from subcensuspi.web.app import create_app

# one burst, three decoders fired on replay: a sane TH sensor (3 intra-burst repeats),
# a second sane interpretation, and the classic impossible-current junk match
FAKE_LINES = [
    '{"model":"Prologue-TH","id":7,"channel":1,"temperature_C":20.5,"humidity":50}',
    '{"model":"Prologue-TH","id":7,"channel":1,"temperature_C":20.5,"humidity":50}',
    '{"model":"Prologue-TH","id":7,"channel":1,"temperature_C":20.5,"humidity":50}',
    '{"model":"Nexus-TH","id":33,"channel":2,"temperature_C":19.9,"humidity":48}',
    '{"model":"Efergy-e2CT","id":512,"current":96.1}',
    "not json",
    '{"no_model":true}',
]


def test_redecode_groups_and_ranks(tmp_path):
    cands = redecode_file(tmp_path / "g001_433.92M_250k.cu8", run=lambda p: FAKE_LINES)
    models = [c["model"] for c in cands]
    assert models[0] in ("Prologue-TH", "Nexus-TH")  # plausible readings outrank junk
    assert models[-1] == "Efergy-e2CT"  # 96.1 A -> bottom, with the reason attached
    assert any("range" in r for r in cands[-1]["reasons"])
    pro = next(c for c in cands if c["model"] == "Prologue-TH")
    assert pro["frames"] == 3  # intra-burst repeats grouped, not three separate candidates
    assert pro["reading"].startswith("temp 20.5")


def test_redecode_marks_catalog_hits(tmp_path):
    db = Database(tmp_path / "c.db")
    for _ in range(10):  # the Prologue is already a known, repeatedly-heard device
        db.ingest(Reception(ts="2026-07-09T00:00:00", model="Prologue-TH", dev_id="7",
                            channel="1", freq_hz=433920000, rssi=-65, snr=9, source="d",
                            place="home", raw_json='{"model":"Prologue-TH","temperature_C":20}'))
    cands = redecode_file(tmp_path / "x.cu8", db=db, run=lambda p: FAKE_LINES)
    pro = next(c for c in cands if c["model"] == "Prologue-TH")
    nex = next(c for c in cands if c["model"] == "Nexus-TH")
    assert pro["in_catalog"] is True and nex["in_catalog"] is False
    assert pro["confidence"] > nex["confidence"]  # catalog corroboration boosts the known device
    db.close()


def test_prune_samples(tmp_path):
    for i in range(6):
        f = tmp_path / f"g00{i}_433.92M_250k.cu8"
        f.write_bytes(b"x" * 100)
        os.utime(f, (1000 + i, 1000 + i))  # deterministic oldest-first order
    removed = prune_samples(tmp_path, max_gb=1, max_files=4)
    assert removed == 2
    left = sorted(f.name for f in tmp_path.glob("*.cu8"))
    assert left == [f"g00{i}_433.92M_250k.cu8" for i in range(2, 6)]  # oldest two gone
    assert prune_samples(tmp_path / "missing") == 0


def test_prune_samples_recursive_and_sweeps_empty_run_dirs(tmp_path):
    """Samples live in per-launch run-* subdirs; prune must recurse and remove emptied run dirs."""
    import os

    for r in range(2):
        rd = tmp_path / f"run-{r:08x}"
        rd.mkdir()
        for i in range(3):
            f = rd / f"g00{i}_433.92M_250k.cu8"
            f.write_bytes(b"x" * 100)
            os.utime(f, (1000 + r * 10 + i, 1000 + r * 10 + i))
    removed = prune_samples(tmp_path, max_gb=1, max_files=3)  # keep newest 3
    assert removed == 3
    assert len(list(tmp_path.rglob("*.cu8"))) == 3
    assert not (tmp_path / "run-00000000").exists()  # fully drained -> swept


def test_sample_endpoints(tmp_path, monkeypatch):
    place = tmp_path / "home" / "iq"
    place.mkdir(parents=True)
    (place / "g001_433.92M_250k.cu8").write_bytes(b"\x00" * 512)
    client = TestClient(create_app(str(tmp_path / "c.db"), place="home",
                                   places_dir=str(tmp_path)))
    # listing
    listing = client.get("/api/samples").json()
    assert len(listing) == 1 and listing[0]["file"] == "g001_433.92M_250k.cu8"
    # redecode: no rtl_433 in CI -> clean 503; with a faked binary+runner -> ranked candidates
    import subcensuspi.web.app as webapp
    monkeypatch.setattr(webapp, "rtl433_available", lambda: True)
    monkeypatch.setattr(webapp, "redecode_file", lambda path, db=None: [
        {"model": "Prologue-TH", "dev_id": "7", "channel": "1", "frames": 3,
         "reading": "temp 20.5°C · humidity 50%", "in_catalog": False,
         "confidence": 0.4, "plausible": False, "reasons": ["heard only once"]}])
    j = client.post("/api/sample/redecode", data={"file": "g001_433.92M_250k.cu8"}).json()
    assert j["candidates"][0]["model"] == "Prologue-TH"
    # traversal guarded + missing file 404
    assert client.post("/api/sample/redecode", data={"file": "../../etc/passwd"}).status_code == 404
    assert client.post("/api/sample/redecode", data={"file": "nope.cu8"}).status_code == 404
