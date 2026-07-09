"""Single web-controlled radio owner (Pi §3, §9): one dongle, mutually-exclusive off/decode/
spectrum, mode persistence for headless boot resume, and the /api/radio control/status endpoints.
No hardware — a fake LiveSweeper stands in for rtl_power and decode is never actually launched
(no config -> a clean 503), so the mutual-exclusion + persistence logic is fully exercised off-device."""

import json

from fastapi.testclient import TestClient

from subcensuspi.radio import MODES, RadioManager
from subcensuspi.web.app import create_app


class FakeLive:
    """Stands in for LiveSweeper: records start/stop, reports running, no subprocess."""

    def __init__(self, available=True):
        self._available = available
        self.running = False
        self.range = None
        self.started_with = []

    def available(self):
        return self._available

    def start(self, band=None, integration_s=1):
        if not self._available:
            raise FileNotFoundError("rtl_power not found")
        self.started_with.append(band)
        self.running = True
        self.range = f"range:{band}"
        return self.range

    def stop(self):
        self.running = False

    def snapshot(self, buckets=120):
        return {"running": self.running, "range": self.range, "error": None,
                "freqs": [], "sweeps": []}


def test_modes_constant():
    assert MODES == ("off", "decode", "spectrum")


def test_off_is_idle():
    r = RadioManager(FakeLive())
    st = r.set_mode("off")
    assert st["mode"] == "off"
    assert st["decode"]["running"] is False and st["spectrum"]["running"] is False


def test_bad_mode_raises():
    r = RadioManager(FakeLive())
    try:
        r.set_mode("nope")
        assert False, "expected ValueError"
    except ValueError:
        pass


def test_spectrum_starts_and_is_exclusive():
    live = FakeLive()
    r = RadioManager(live)
    st = r.set_mode("spectrum", band="433")
    assert st["mode"] == "spectrum" and st["spectrum"]["running"] is True
    assert live.started_with == ["433"] and r._band == "433"
    # switching to off stops the sweep (one radio)
    st = r.set_mode("off")
    assert st["mode"] == "off" and live.running is False


def test_spectrum_unavailable_propagates_filenotfound():
    r = RadioManager(FakeLive(available=False))
    try:
        r.set_mode("spectrum")
        assert False, "expected FileNotFoundError"
    except FileNotFoundError:
        pass


def test_decode_without_config_raises_runtime():
    r = RadioManager(FakeLive(), config_path=None)
    try:
        r.set_mode("decode")
        assert False, "expected RuntimeError"
    except RuntimeError:
        pass
    assert r.decode_available() is False


def test_persist_and_resume(tmp_path):
    state = tmp_path / "radio_state.json"
    live = FakeLive()
    r = RadioManager(live, state_path=str(state))
    r.set_mode("spectrum", band="315")
    saved = json.loads(state.read_text())
    assert saved == {"mode": "spectrum", "band": "315"}
    # a fresh manager resumes the persisted spectrum mode from disk
    live2 = FakeLive()
    r2 = RadioManager(live2, state_path=str(state))
    r2.resume()
    assert r2.status()["mode"] == "spectrum" and live2.running is True


def test_resume_off_is_noop(tmp_path):
    state = tmp_path / "radio_state.json"
    state.write_text(json.dumps({"mode": "off", "band": "ism"}))
    live = FakeLive()
    r = RadioManager(live, state_path=str(state))
    r.resume()
    assert r.status()["mode"] == "off" and live.running is False


def test_resume_swallows_hardware_error(tmp_path):
    """A saved spectrum mode with rtl_power missing at boot must NOT crash — stay off, note why."""
    state = tmp_path / "radio_state.json"
    state.write_text(json.dumps({"mode": "spectrum", "band": "ism"}))
    r = RadioManager(FakeLive(available=False), state_path=str(state))
    r.resume()  # must not raise
    st = r.status()
    assert st["mode"] == "off" and st["error"] and "resume" in st["error"]


# --- endpoints ---

def test_radio_endpoints(tmp_path):
    client = TestClient(create_app(str(tmp_path / "c.db")))
    # status is always available
    st = client.get("/api/radio").json()
    assert st["mode"] == "off" and set(st["modes"]) == set(MODES)
    # off is always safe
    assert client.post("/api/radio", data={"mode": "off"}).json()["mode"] == "off"
    # bad mode -> 400
    assert client.post("/api/radio", data={"mode": "bogus"}).status_code == 400
    # decode with no SUBCENSUSPI_CONFIG -> 503 (clean, not a crash)
    assert client.post("/api/radio", data={"mode": "decode"}).status_code == 503
    # spectrum -> 200 if a real rtl_power is installed, else 503 (rtl_power missing)
    assert client.post("/api/radio", data={"mode": "spectrum", "band": "ism"}).status_code in (200, 503)
    # leave the radio off
    client.post("/api/radio", data={"mode": "off"})
