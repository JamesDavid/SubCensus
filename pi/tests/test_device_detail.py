"""Device detail page + multi-candidate decode (System §6, Pi §7). The detail page lists every
reception ("all communications") from a device; candidate fingerprints group the decodes that
landed on the SAME burst (same time+freq) and rank them by the confidence gate. Full-protocol
capture (-G 4) is what produces multiple candidates going forward. No hardware."""

from fastapi.testclient import TestClient

from subcensuspi.collector.rtl433 import build_argv
from subcensuspi.config import Config, DongleConfig
from subcensuspi.db import Database, Reception
from subcensuspi.web.app import create_app


def test_all_protocols_flag_in_argv():
    dc = DongleConfig(freqs=["433.92M"])
    assert "-G" not in build_argv(dc)  # default: rtl_433's default decoder set
    argv = build_argv(dc, all_protocols=True)
    assert argv[argv.index("-G") + 1] == "4"  # -G 4 enables EVERY decoder (all candidates)


def test_config_parses_all_protocols():
    assert Config.from_dict({"all_protocols": True}).all_protocols is True
    assert Config.from_dict({}).all_protocols is False


def _seed(tmp_path):
    db = Database(tmp_path / "c.db")
    # a real device heard many times
    for i in range(6):
        db.ingest(Reception(ts=f"2026-07-09T00:00:0{i}", model="Acurite-Tower", dev_id="1",
                            channel="A", freq_hz=433920000, rssi=-60, snr=12, source="d",
                            place="home", raw_json='{"model":"Acurite-Tower","temperature_C":21,"humidity":45}'))
    # TWO decoders that fired on the SAME burst (same ts + freq): a plausible one and junk.
    burst_ts = "2026-07-09T09:00:00"
    db.ingest(Reception(ts=burst_ts, model="Prologue-TH", dev_id="7", channel="1",
                        freq_hz=433920000, rssi=-65, snr=9, source="d", place="home",
                        raw_json='{"model":"Prologue-TH","temperature_C":20,"humidity":50}'))
    db.ingest(Reception(ts=burst_ts, model="Efergy-e2CT", dev_id="512", channel="",
                        freq_hz=433930000, rssi=-65, snr=9, source="d", place="home",
                        raw_json='{"model":"Efergy-e2CT","current":96.1}'))
    db.close()
    return str(tmp_path / "c.db")


def test_detail_page_lists_all_communications(tmp_path):
    from subcensuspi.db import device_id_for
    client = TestClient(create_app(_seed(tmp_path)))
    did = device_id_for("Acurite-Tower", "1", "A")
    r = client.get(f"/device/{did}")
    assert r.status_code == 200
    assert "Communications" in r.text and "temp 21°C" in r.text
    # every reception row present (6 receptions -> the humanized reading appears once per row)
    assert r.text.count("temp 21°C") == 6


def test_candidate_fingerprints_ranked(tmp_path):
    from subcensuspi.db import device_id_for
    client = TestClient(create_app(_seed(tmp_path)))
    did = device_id_for("Prologue-TH", "7", "1")
    j = client.get(f"/api/device/{did}/candidates").json()
    models = [c["model"] for c in j["candidates"]]
    # both decoders that hit the same burst are surfaced as candidates...
    assert "Prologue-TH" in models and "Efergy-e2CT" in models
    # ...ranked by confidence: the plausible Prologue outranks the 96 A Efergy
    assert j["candidates"][0]["model"] == "Prologue-TH"
    assert j["candidates"][0]["confidence"] > j["candidates"][-1]["confidence"]
    assert any("range" in reason for reason in j["candidates"][-1]["reasons"])


def test_detail_404(tmp_path):
    client = TestClient(create_app(_seed(tmp_path)))
    assert client.get("/device/nope").status_code == 404
    assert client.get("/api/device/nope/candidates").status_code == 404
