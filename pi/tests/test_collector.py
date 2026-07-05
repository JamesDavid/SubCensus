"""M1: collector rtl_433 JSON -> SQLite (Pi §5). Driven from recorded JSON — no hardware."""

import pytest

from subcensuspi.collector.collector import Collector
from subcensuspi.collector.parser import parse_line
from subcensuspi.collector.rtl433 import build_argv, replay_file
from subcensuspi.config import DongleConfig
from subcensuspi.db import Database, device_id_for
from subcensuspi.dsp import cadence


@pytest.fixture
def stream(fixtures_dir):
    return fixtures_dir / "rtl433" / "home_stream.jsonl"


@pytest.fixture
def db(tmp_path):
    d = Database(tmp_path / "census.db")
    yield d
    d.close()


def test_parser_decoded_and_undecoded():
    r = parse_line('{"time":"t","model":"Acurite-Tower","id":1234,"channel":"A","freq":433.92,"snr":12.0}')
    assert r is not None
    assert r.model == "Acurite-Tower" and r.dev_id == "1234" and r.channel == "A"
    assert r.freq_hz == 433920000 and r.snr == 12.0
    # stats / pulse lines have no model
    assert parse_line('{"type":"stats","stats":{}}') is None
    assert parse_line("not json") is None


def test_collector_rolls_up_devices(db, stream):
    c = Collector(db, place="home", capture_unknowns=False)
    c.process_stream(replay_file(stream))
    # 4 distinct decoded devices (Acurite, Prologue, Schrader, Generic); stats+pulse skipped
    assert db.device_count() == 4
    assert db.event_count() == 7  # 4 Acurite + 1 each of the other three
    assert c.stats.decoded == 7
    assert c.stats.skipped == 2  # stats line + unknown-pulse line (unknowns off)


def test_device_grouping_and_rollup(db, stream):
    c = Collector(db, place="home")
    c.process_stream(replay_file(stream))
    did = device_id_for("Acurite-Tower", "1234", "A")
    dev = db.get_device(did)
    assert dev is not None
    assert dev["count"] == 4
    assert dev["first_seen"] == "2026-07-04T12:00:00"
    assert dev["last_seen"] == "2026-07-04T12:03:00"
    assert dev["typical_freq_hz"] == 433920000
    assert abs(dev["avg_snr"] - 12.0) < 0.1  # (12.0+11.5+12.5+12.0)/4
    assert dev["place"] == "home"


def test_cadence_from_events_ties_to_dsp(db, stream):
    """The collector's events feed the DSP cadence estimator (Pi §5 — strongest measurer)."""
    c = Collector(db, place="home")
    c.process_stream(replay_file(stream))
    did = device_id_for("Acurite-Tower", "1234", "A")
    ts = db.device_event_timestamps(did)
    assert len(ts) == 4
    # timestamps are ISO; convert to epoch-ish seconds for cadence (60 s apart)
    import datetime as dt
    secs = [int(dt.datetime.fromisoformat(t).timestamp()) for t in ts]
    est = cadence.from_timestamps(secs)
    assert est.cls in (cadence.PERIODIC, cadence.QUASI_PERIODIC)
    assert abs(est.period_s - 60) <= 2


def test_manual_labeling(db, stream):
    c = Collector(db, place="home")
    c.process_stream(replay_file(stream))
    did = device_id_for("Acurite-Tower", "1234", "A")
    db.set_device_label(did, label="Garden temp", room="Garden", device_class="weather")
    dev = db.get_device(did)
    assert dev["label"] == "Garden temp"
    assert dev["room"] == "Garden"
    assert dev["device_class"] == "weather"


def test_capture_unknowns_routes_to_unknowns_table(db, stream):
    c = Collector(db, place="home", capture_unknowns=True)
    c.process_stream(replay_file(stream))
    rows = db.conn.execute("SELECT COUNT(*) c FROM unknowns").fetchone()["c"]
    assert rows == 1  # the mod/pulses line with no model
    assert c.stats.unknowns == 1


def test_build_argv_hop():
    argv = build_argv(DongleConfig(serial="00000001", freqs=["433.92M", "915M"], hop_seconds=30))
    assert "rtl_433" in argv[0]
    assert "-F" in argv and "json" in argv
    assert argv.count("-f") == 2
    assert "-H" in argv and "30" in argv
    assert "-d" in argv and ":00000001" in argv
    # §4 baseline: -M stats (periodic health) + -Y autolevel (adaptive detection level)
    stats_at = [i for i, a in enumerate(argv) if a == "-M" and argv[i + 1] == "stats"]
    assert stats_at, "expected -M stats in the §4 baseline"
    y_at = argv.index("-Y")
    assert argv[y_at + 1] == "autolevel"
