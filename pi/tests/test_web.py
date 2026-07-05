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
