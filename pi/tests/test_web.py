"""M2: FastAPI dashboard (Pi §7) — httpx/TestClient assertions on served HTML + JSON API."""

import pytest
from fastapi.testclient import TestClient

from subcensuspi.collector.collector import Collector
from subcensuspi.collector.rtl433 import replay_file
from subcensuspi.db import Database, device_id_for
from subcensuspi.web.app import create_app


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
