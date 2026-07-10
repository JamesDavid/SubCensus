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


def test_teardown_does_not_persist_off(tmp_path):
    """Shutdown must free the dongle WITHOUT persisting 'off' — else every restart resumes off
    and the census never comes back. The saved mode survives teardown."""
    import json

    state = tmp_path / "radio_state.json"
    r = RadioManager(FakeLive(), state_path=str(state))
    r.set_mode("spectrum", band="433")
    assert json.loads(state.read_text())["mode"] == "spectrum"
    r.teardown()
    assert json.loads(state.read_text())["mode"] == "spectrum"  # NOT clobbered to off
    # a fresh manager resumes the persisted mode after the "restart"
    r2 = RadioManager(FakeLive(), state_path=str(state))
    r2.resume()
    assert r2.status()["mode"] == "spectrum"


def test_spectrum_stop_resumes_prior_mode():
    """A spectrum look is a detour: after it, the radio returns to whatever ran before (usually
    the 24/7 census), instead of persisting 'off' and silently killing decode across reboots."""
    r = RadioManager(FakeLive())
    r._mode = "decode"  # pretend the census was running (no subprocess needed for bookkeeping)
    r.set_mode("spectrum", band="433")
    assert r.after_spectrum_mode() == "decode"
    # from off, a spectrum look returns to off
    r2 = RadioManager(FakeLive())
    r2.set_mode("spectrum")
    assert r2.after_spectrum_mode() == "off"
    # explicitly picking a non-spectrum mode clears the detour memory
    r.set_mode("off")
    assert r.after_spectrum_mode() == "off"


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
