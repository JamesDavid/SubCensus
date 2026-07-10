"""M2: FastAPI dashboard (Pi §7) — httpx/TestClient assertions on served HTML + JSON API."""

import pytest
from fastapi.testclient import TestClient

from subcensuspi.collector.collector import Collector
from subcensuspi.collector.rtl433 import replay_file
from subcensuspi.db import Database, device_id_for
from subcensuspi.web.app import activity_buckets, create_app, sparkline


def test_activity_buckets_and_sparkline_edge_cases():
    assert activity_buckets([], 8) == [0] * 8
    assert sparkline([0] * 8) == ""  # no activity -> empty
    # a single reception lands in the newest bin
    one = activity_buckets(["2026-07-04T12:00:00"], 8)
    assert sum(one) == 1 and one[-1] == 1
    # evenly spread receptions fill distinct bins; sparkline is a unicode-block string
    spread = activity_buckets(
        ["2026-07-04T12:00:00", "2026-07-04T12:01:00", "2026-07-04T12:02:00"], 6
    )
    assert sum(spread) == 3
    assert all(ch in " ▁▂▃▄▅▆▇█" for ch in sparkline(spread)) and sparkline(spread)


@pytest.fixture
def populated_db(tmp_path, fixtures_dir):
    path = tmp_path / "census.db"
    db = Database(path)
    Collector(db, place="home").process_stream(
        replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl")
    )
    db.close()
    return str(path)


@pytest.fixture
def client(populated_db):
    return TestClient(create_app(populated_db))


def test_index_renders_devices(client):
    r = client.get("/")
    assert r.status_code == 200
    assert "Acurite-Tower" in r.text


def test_recon_parks_and_resumes_radio(tmp_path, monkeypatch):
    """One radio: a recon sweep must park the current mode (usually decode) and resume it after —
    otherwise it always fails dongle-busy in normal 24/7 operation (the old two-service reflex)."""
    import subcensuspi.web.app as webapp

    (tmp_path / "home").mkdir()
    app = create_app(str(tmp_path / "c.db"), place="home", places_dir=str(tmp_path))

    calls = []

    class StubRadio:
        def status(self):
            return {"mode": "decode"}

        def set_mode(self, mode, band=None):
            calls.append(mode)
            return {"mode": mode}

    app.state.radio = StubRadio()
    monkeypatch.setattr(webapp, "rtl_power_available", lambda: True)

    def fake_sweep(out_path, *, freq_range, duration_s):
        # a real rtl_power CSV line (float step, one hop) so the pass parses it
        out_path.write_text(
            "2026-07-08, 17:02:06, 433000000, 434000000, 1000000.00, 1, -95.0, -55.0\n",
            encoding="utf-8",
        )

    monkeypatch.setattr(webapp, "sweep_live_to_csv", fake_sweep)
    client = TestClient(app)
    r = client.post("/api/recon/run", data={"band": "433"})
    assert r.status_code == 200 and r.json()["ok"] is True
    assert calls == ["off", "decode"]  # parked for the sweep, census resumed after


def test_radio_status_reports_decode_health(tmp_path):
    """/api/radio carries last_event_age_s — the honest 'is the census actually hearing things'
    signal (a live collector subprocess proves nothing if rtl_433 crash-loops inside it)."""
    from subcensuspi.db import Database, Reception

    db = Database(tmp_path / "c.db")
    db.ingest(Reception(ts="2026-07-09T00:00:00", model="Acurite-Tower", dev_id="1", channel="A",
                        freq_hz=433920000, rssi=-60, snr=12, source="d", place="home",
                        raw_json='{"model":"Acurite-Tower","temperature_C":21}'))
    db.close()
    client = TestClient(create_app(str(tmp_path / "c.db")))
    j = client.get("/api/radio").json()
    assert j["last_event_ts"] == "2026-07-09T00:00:00"
    assert isinstance(j["last_event_age_s"], int) and j["last_event_age_s"] >= 0
    # empty catalog -> explicit "nothing decoded yet" signal, not a crash
    client2 = TestClient(create_app(str(tmp_path / "empty.db")))
    assert client2.get("/api/radio").json()["last_event_age_s"] is None


def test_confidence_gate_hides_implausible(tmp_path):
    """System §6 honesty gate: physically-implausible / uncorroborated decodes are hidden by
    default and revealed with show_all. Display-only — nothing is deleted."""
    from subcensuspi.db import Reception

    db = Database(tmp_path / "c.db")
    # a real, repeatedly-heard sensor with a sane reading -> confident
    for _ in range(6):
        db.ingest(Reception(ts="2026-07-09T00:00:00", model="Acurite-Tower", dev_id="1",
                            channel="A", freq_hz=433920000, rssi=-60, snr=12, source="d",
                            place="home", raw_json='{"model":"Acurite-Tower","temperature_C":21,"humidity":45}'))
    # a phantom: an Efergy claiming 96 A, heard once -> implausible + uncorroborated
    db.ingest(Reception(ts="2026-07-09T01:00:00", model="Efergy-e2CT", dev_id="512",
                        channel="", freq_hz=315000000, rssi=-70, snr=10, source="d",
                        place="home", raw_json='{"model":"Efergy-e2CT","current":96.1}'))
    # a phantom: an Opus at its -40 °C null value -> implausible sentinel
    db.ingest(Reception(ts="2026-07-09T02:00:00", model="Opus-XT300", dev_id="0",
                        channel="", freq_hz=314990000, rssi=-60, snr=19, source="d",
                        place="home", raw_json='{"model":"Opus-XT300","moisture":0,"temperature_C":-40}'))
    db.close()
    client = TestClient(create_app(str(tmp_path / "c.db")))

    # default view keeps the real sensor, hides both phantoms
    r = client.get("/")
    assert "Acurite-Tower" in r.text
    assert "Efergy-e2CT" not in r.text and "Opus-XT300" not in r.text
    assert "2 hidden as low-confidence" in r.text

    # show-all reveals them, with the why-not reason in the title attribute
    r_all = client.get("/?show_all=1")
    assert "Efergy-e2CT" in r_all.text and "Opus-XT300" in r_all.text
    assert "null value" in r_all.text  # the -40 °C sentinel explanation
    assert "Live feed" in r.text
    assert "weather" in r.text  # taxonomy class in the label picker
    # §7 devices table columns: last-seen + activity sparkline
    assert "Last seen" in r.text
    assert "Activity" in r.text


def test_index_renders_activity_sparkline(client):
    r = client.get("/")
    # Acurite-Tower has 4 receptions spread across ~3 min -> a non-empty unicode-block sparkline
    assert 'class="spark"' in r.text
    assert any(block in r.text for block in "▁▂▃▄▅▆▇█")


def test_device_activity_endpoint(client):
    did = device_id_for("Acurite-Tower", "1234", "A")
    r = client.get(f"/api/device/{did}/activity?buckets=12")
    assert r.status_code == 200
    body = r.json()
    assert len(body["buckets"]) == 12
    assert sum(body["buckets"]) == 4  # 4 receptions in the fixture
    assert isinstance(body["sparkline"], str) and body["sparkline"]
    assert client.get("/api/device/deadbeef/activity").status_code == 404


def test_api_devices(client):
    r = client.get("/api/devices")
    assert r.status_code == 200
    devices = r.json()
    assert len(devices) == 4
    models = {d["model"] for d in devices}
    assert "Acurite-Tower" in models and "Schrader" in models


def test_api_events(client):
    r = client.get("/api/events?limit=5")
    assert r.status_code == 200
    assert len(r.json()) == 5


def test_api_device_detail_and_404(client):
    did = device_id_for("Acurite-Tower", "1234", "A")
    r = client.get(f"/api/device/{did}")
    assert r.status_code == 200
    assert r.json()["count"] == 4
    assert client.get("/api/device/deadbeef").status_code == 404


def test_inline_labeling_roundtrip(client):
    did = device_id_for("Acurite-Tower", "1234", "A")
    r = client.post(
        f"/api/device/{did}/label",
        data={"label": "Garden temp", "room": "Garden", "device_class": "weather"},
    )
    assert r.status_code == 200 and r.json()["ok"]
    dev = client.get(f"/api/device/{did}").json()
    assert dev["label"] == "Garden temp"
    assert dev["room"] == "Garden"
    assert dev["device_class"] == "weather"


def test_label_rejects_invalid_class(client):
    did = device_id_for("Acurite-Tower", "1234", "A")
    r = client.post(f"/api/device/{did}/label", data={"device_class": "not-a-class"})
    assert r.status_code == 400


def test_label_404_for_unknown_device(client):
    r = client.post("/api/device/deadbeef/label", data={"label": "x"})
    assert r.status_code == 404
