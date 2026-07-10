"""Admin/test API (gated by SUBCENSUSPI_ADMIN_API): drive the pipeline with no dongle and deploy
over HTTP. These endpoints let the whole decode->classify->catalog path be exercised and verified
remotely; they 403 unless explicitly enabled. No hardware."""

import csv

from fastapi.testclient import TestClient

from subcensuspi.web.app import create_app


def _brain_dir(tmp_path):
    d = tmp_path / "sig"
    d.mkdir()
    with (d / "protocol_map.csv").open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["protocol", "friendly_name", "device_class",
                                           "typical_use", "notes"])
        w.writeheader()
        w.writerow({"protocol": "Acurite-Tower", "friendly_name": "Acurite Tower",
                    "device_class": "weather", "typical_use": "", "notes": ""})
    return d


def test_admin_gated_off_by_default(tmp_path):
    client = TestClient(create_app(str(tmp_path / "c.db")))  # admin_api defaults False
    assert client.post("/api/test/ingest", data={"events": "[]"}).status_code == 403
    assert client.get("/api/admin/version").status_code == 403
    assert client.post("/api/admin/update").status_code == 403


def test_test_ingest_runs_full_pipeline(tmp_path):
    client = TestClient(create_app(str(tmp_path / "c.db"), place="home",
                                   signatures_dir=str(_brain_dir(tmp_path)), admin_api=True))
    events = ('[{"model":"Acurite-Tower","id":1,"channel":"A","freq":433.92,'
              '"temperature_C":21,"humidity":45,"snr":12}]')
    j = client.post("/api/test/ingest", data={"events": events}).json()
    assert j["decoded"] == 1 and j["devices"] == 1
    # the brain classified it end-to-end through the real Collector path
    dev = client.get("/api/devices").json()[0]
    assert dev["match_name"] == "Acurite Tower" and dev["match_class"] == "weather"


def test_test_ingest_accepts_newline_json(tmp_path):
    client = TestClient(create_app(str(tmp_path / "c.db"), place="home", admin_api=True))
    events = '{"model":"Foo","id":1,"freq":315.0,"snr":9}\n{"model":"Foo","id":2,"freq":315.0}'
    j = client.post("/api/test/ingest", data={"events": events}).json()
    assert j["decoded"] == 2 and j["devices"] == 2


def test_active_learning_via_label(tmp_path):
    sig = tmp_path / "sig2"
    client = TestClient(create_app(str(tmp_path / "c.db"), place="home",
                                   signatures_dir=str(sig), admin_api=True))
    client.post("/api/test/ingest",
                data={"events": '[{"model":"Widget-X","id":9,"freq":315.0,"snr":8}]'})
    dev = client.get("/api/devices").json()[0]
    assert dev["match_name"] is None  # Widget-X not in the brain yet
    # user confirms a class -> brain learns it
    r = client.post(f"/api/device/{dev['device_id']}/label",
                    data={"label": "My Widget", "device_class": "remote"}).json()
    assert r["learned"] is True
    # a NEW device of the same model is now auto-classified
    client.post("/api/test/ingest",
                data={"events": '[{"model":"Widget-X","id":10,"freq":315.0,"snr":8}]'})
    matches = [d for d in client.get("/api/devices").json() if d["dev_id"] == "10"]
    assert matches and matches[0]["match_class"] == "remote"


def test_version_endpoint(tmp_path):
    client = TestClient(create_app(str(tmp_path / "c.db"), admin_api=True,
                                   repo_dir=str(tmp_path)))
    j = client.get("/api/admin/version").json()
    assert "commit" in j and "brain_rows" in j and j["repo_dir"] == str(tmp_path)


def test_update_requires_repo_dir(tmp_path):
    client = TestClient(create_app(str(tmp_path / "c.db"), admin_api=True))  # no repo_dir
    assert client.post("/api/admin/update").status_code == 400
