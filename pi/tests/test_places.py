"""M8: Places scoping across tables + /api/places + place-filtered API (Pi §9a)."""

import pytest
from fastapi.testclient import TestClient

from subcensuspi.collector.collector import Collector
from subcensuspi.collector.rtl433 import replay_file
from subcensuspi.db import Database
from subcensuspi.web.app import create_app


@pytest.fixture
def two_place_db(tmp_path, fixtures_dir):
    path = tmp_path / "census.db"
    db = Database(path)
    Collector(db, place="home").process_stream(replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl"))
    Collector(db, place="truck").process_stream(replay_file(fixtures_dir / "rtl433" / "garage_915.jsonl"))
    db.close()
    return str(path)


def test_place_column_scopes_all_tables(two_place_db):
    db = Database(two_place_db)
    assert db.distinct_places() == ["home", "truck"]
    assert db.device_count("home") == 4
    assert db.device_count("truck") == 2
    # events carry place too
    home_ev = db.conn.execute("SELECT COUNT(*) c FROM events WHERE place='home'").fetchone()["c"]
    truck_ev = db.conn.execute("SELECT COUNT(*) c FROM events WHERE place='truck'").fetchone()["c"]
    assert home_ev == 7 and truck_ev == 4
    assert {r["model"] for r in db.list_devices("truck")} == {"Acurite-606TX", "ERT-SCM"}
    db.close()


def test_api_places_and_filtering(two_place_db):
    client = TestClient(create_app(two_place_db, place="home"))
    places = client.get("/api/places").json()
    assert places["active"] == "home"
    assert set(places["places"]) == {"home", "truck"}

    home = client.get("/api/devices?place=home").json()
    assert len(home) == 4
    truck = client.get("/api/devices?place=truck").json()
    assert len(truck) == 2
    assert all(d["place"] == "truck" for d in truck)
