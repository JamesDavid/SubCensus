"""Device detail page + multi-candidate decode (System §6, Pi §7). The detail page lists every
reception ("all communications") from a device; candidate fingerprints group the decodes that
landed on the SAME burst (same time+freq) and rank them by the confidence gate. Full-protocol
capture of raw .cu8 bursts (-S all) + offline replay is what tests a signal against every
decoder going forward. No hardware."""

from fastapi.testclient import TestClient

from subcensuspi.collector.rtl433 import build_argv
from subcensuspi.config import Config, DongleConfig
from subcensuspi.db import Database, Reception
from subcensuspi.web.app import create_app


def test_live_argv_has_no_undocumented_flags():
    """-G is undocumented on packaged rtl_433 (bookworm man page) — an unsupported flag makes
    rtl_433 exit instantly and crash-loop the census. It must NEVER be in the live argv; the
    full decoder set is only tried on offline replay (redecode), where failure is catchable."""
    dc = DongleConfig(freqs=["433.92M"])
    assert "-G" not in build_argv(dc)
    assert "-G" not in build_argv(dc, samples=True)


def test_samples_flag_in_argv():
    dc = DongleConfig(freqs=["433.92M"])
    assert "-S" not in build_argv(dc)  # off unless a capture dir is set
    argv = build_argv(dc, samples=True)
    assert argv[argv.index("-S") + 1] == "all"  # every burst saved as a raw .cu8


def test_baseline_keeps_raw_bits():
    # -M bits adds raw bit representation where decoders expose code outputs (cheap bonus)
    argv = build_argv(DongleConfig(freqs=["433.92M"]))
    assert "bits" in argv and argv[argv.index("bits") - 1] == "-M"


def test_config_parses_capture_samples():
    assert Config.from_dict({}).capture_samples is True  # DEFAULT ON: keep the received bits
    assert Config.from_dict({"capture_samples": False}).capture_samples is False
    assert Config.from_dict({"max_samples_gb": 5}).max_samples_gb == 5.0
    # legacy key from the brief -G experiment is simply ignored, not a crash
    assert Config.from_dict({"all_protocols": True}).capture_samples is True


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


def test_detail_shows_nearby_captured_bursts(tmp_path):
    """Samples captured within ±3 s of a device's receptions appear on its detail page with a
    re-decode button; unrelated (far-in-time) samples don't."""
    import os
    from datetime import datetime

    from subcensuspi.db import device_id_for
    db_path = _seed(tmp_path)
    iq = tmp_path / "home" / "iq"
    iq.mkdir(parents=True)
    near = iq / "g001_433.92M_250k.cu8"
    far = iq / "g002_433.92M_250k.cu8"
    near.write_bytes(b"\x00" * 256)
    far.write_bytes(b"\x00" * 256)
    ts = datetime.fromisoformat("2026-07-09T00:00:03").timestamp()  # within ±3 s of a reception
    os.utime(near, (ts, ts))
    os.utime(far, (ts + 999999, ts + 999999))
    client = TestClient(create_app(db_path, place="home", places_dir=str(tmp_path)))
    r = client.get(f"/device/{device_id_for('Acurite-Tower', '1', 'A')}")
    assert "Captured bursts" in r.text
    assert "g001_433.92M_250k.cu8" in r.text and "g002_433.92M_250k.cu8" not in r.text
    assert "match against all decoders" in r.text


def test_detail_404(tmp_path):
    client = TestClient(create_app(_seed(tmp_path)))
    assert client.get("/device/nope").status_code == 404
    assert client.get("/api/device/nope/candidates").status_code == 404
