"""Continuous live-spectrum streaming (Pi §7): shared rtl_power line parse, the LiveSweeper ring
buffer/snapshot, and the /api/spectrum/live control + poll endpoints. No hardware — the buffer is
fed directly; the live start path surfaces a clean 503 when rtl_power is absent."""

from fastapi.testclient import TestClient

from subcensuspi import occupancy_pass as op
from subcensuspi.live_sweep import LiveSweeper
from subcensuspi.web.app import create_app


def test_parse_rtl_power_line_shared():
    p = op.parse_rtl_power_line(
        "2026-07-08, 06:49:41, 433000000, 434000000, 1000000.00, 1, -95.0, -55.0"
    )
    assert p is not None
    ts, band_low, pairs = p
    assert ts == "2026-07-08T06:49:41" and band_low == 433000000
    assert pairs == [(433250000, -95.0), (433750000, -55.0)]  # float step, binned by dBm count
    assert op.parse_rtl_power_line("# hdr") is None and op.parse_rtl_power_line("") is None


def test_resolve_range_presets():
    assert op.resolve_range("ism") == op.SWEEP_PRESETS["ism"]
    assert op.resolve_range("433") == op.SWEEP_PRESETS["433"]
    assert op.resolve_range("300M:470M:25k") == "300M:470M:25k"  # raw passthrough
    assert op.resolve_range(None) == op.DEFAULT_SWEEP_RANGE


def test_live_sweeper_ring_and_snapshot():
    ls = LiveSweeper(max_sweeps=2)
    ls._push("t1", {433000000: -90.0, 433500000: -50.0})
    ls._push("t2", {433000000: -88.0, 433500000: -52.0})
    ls._push("t3", {433000000: -80.0, 433500000: -55.0})  # evicts t1 (maxlen=2)
    snap = ls.snapshot(buckets=8)
    assert len(snap["freqs"]) == 8
    assert len(snap["sweeps"]) == 2  # ring capped
    assert all(len(s["dbm"]) == 8 for s in snap["sweeps"])
    assert snap["sweeps"][-1]["ts"] == "t3"


def test_live_endpoints(tmp_path):
    client = TestClient(create_app(str(tmp_path / "c.db")))
    # idle snapshot
    j = client.get("/api/spectrum/live").json()
    assert j["running"] is False and j["sweeps"] == []
    # start: 503 when rtl_power is absent (CI), 200 if a real rtl_power is installed
    r = client.post("/api/spectrum/live", data={"action": "start", "band": "ism"})
    assert r.status_code in (200, 503)
    # stop is always safe/idempotent
    assert client.post("/api/spectrum/live", data={"action": "stop"}).json()["running"] is False
    # bad action rejected
    assert client.post("/api/spectrum/live", data={"action": "nope"}).status_code == 400
