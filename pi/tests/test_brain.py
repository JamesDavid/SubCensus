"""Classification brain wired into live decode (System §6): protocol_map lookup on each decode
writes the match_* proposal (never auto-relabel), and a user-confirmed label teaches the brain so
future decodes classify immediately. Events retention keeps the log bounded. No hardware."""

import csv

from subcensuspi.brain import Brain
from subcensuspi.collector.collector import Collector
from subcensuspi.db import Database, Reception, device_id_for


def _write_pm(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["protocol", "friendly_name", "device_class",
                                           "typical_use", "notes"])
        w.writeheader()
        for r in rows:
            w.writerow(r)


def test_classify_from_protocol_map(tmp_path):
    _write_pm(tmp_path / "protocol_map.csv", [
        {"protocol": "Acurite-Tower", "friendly_name": "Acurite Tower", "device_class": "weather",
         "typical_use": "temp/humidity", "notes": ""}])
    b = Brain(tmp_path)
    m = b.classify("Acurite-Tower")
    assert m == {"name": "Acurite Tower", "device_class": "weather", "confidence": 0.9,
                 "source": "decoder"}
    assert b.classify("Totally-Unknown-Model") is None  # unmapped decode -> user should label


def test_collector_writes_match_but_not_user_class(tmp_path):
    _write_pm(tmp_path / "sig" / "protocol_map.csv", [
        {"protocol": "Acurite-Tower", "friendly_name": "Acurite Tower", "device_class": "weather",
         "typical_use": "", "notes": ""}])
    db = Database(tmp_path / "c.db")
    c = Collector(db, place="home", brain=Brain(tmp_path / "sig"))
    c.process_line('{"model":"Acurite-Tower","id":1,"channel":"A","freq":433.92,'
                   '"temperature_C":21,"snr":12}', source="t")
    row = db.get_device(device_id_for("Acurite-Tower", "1", "A"))
    assert row["match_name"] == "Acurite Tower" and row["match_class"] == "weather"
    assert row["match_source"] == "decoder" and row["match_confidence"] == 0.9
    assert row["device_class"] is None  # PROPOSAL only — never auto-relabels the user's field
    db.close()


def test_active_learning_upsert_and_reload(tmp_path):
    sig = tmp_path / "sig"
    b = Brain(sig)  # empty runtime -> falls back to packaged seed (may or may not have the model)
    assert b.classify("MadeUp-Widget-9000") is None
    assert b.learn("MadeUp-Widget-9000", friendly_name="My Widget", device_class="remote") is True
    # persisted to the runtime brain...
    assert (sig / "protocol_map.csv").is_file()
    # ...and a freshly loaded brain classifies it (survives a restart)
    assert Brain(sig).classify("MadeUp-Widget-9000") == {
        "name": "My Widget", "device_class": "remote", "confidence": 0.9, "source": "decoder"}


def test_learn_noop_without_signatures_dir():
    assert Brain(None).learn("X", device_class="remote") is False  # nowhere to persist


def test_prune_events_keeps_newest(tmp_path):
    db = Database(tmp_path / "c.db")
    for i in range(20):
        db.ingest(Reception(ts=f"2026-07-09T00:00:{i:02d}", model="M", dev_id="1", channel="",
                            freq_hz=433920000, rssi=-60, snr=10, source="t", place="home",
                            raw_json='{"model":"M"}'))
    assert db.event_count() == 20
    deleted = db.prune_events(keep=5)
    assert deleted == 15 and db.event_count() == 5
    # device roll-up survives the prune (count stays 20, not recomputed from raw events)
    assert db.get_device(device_id_for("M", "1", ""))["count"] == 20
    assert db.prune_events(keep=0) == 0  # 0 = unbounded, no-op
    db.close()
