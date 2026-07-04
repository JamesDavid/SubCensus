"""M3: multi-dongle parallel + source tagging (Pi §3). Two recorded streams as producers."""

import pytest

from subcensuspi.collector.collector import Collector
from subcensuspi.collector.multi import SourceStream, run_multi
from subcensuspi.collector.rtl433 import replay_file
from subcensuspi.db import Database, device_id_for


@pytest.fixture
def db(tmp_path):
    d = Database(tmp_path / "census.db")
    yield d
    d.close()


def test_multi_merges_and_tags_source(db, fixtures_dir):
    c = Collector(db, place="home")
    streams = [
        SourceStream(replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl"), "dongle-433"),
        SourceStream(replay_file(fixtures_dir / "rtl433" / "garage_915.jsonl"), "dongle-915"),
    ]
    run_multi(c, streams)

    # 4 devices from 433 stream + 2 from 915 stream = 6
    assert db.device_count() == 6
    # 7 decoded (433) + 4 decoded (915) = 11 events
    assert db.event_count() == 11

    # each event is tagged with its originating dongle/band
    sources = {r["source"] for r in db.conn.execute("SELECT DISTINCT source FROM events")}
    assert sources == {"dongle-433", "dongle-915"}

    # a 915 device carries only the 915 source
    ert = device_id_for("ERT-SCM", "770123", "")
    rows = db.conn.execute("SELECT DISTINCT source FROM events WHERE device_id=?", (ert,)).fetchall()
    assert {r["source"] for r in rows} == {"dongle-915"}
    assert db.get_device(ert)["count"] == 2

    # a 433 device carries only the 433 source
    acurite = device_id_for("Acurite-Tower", "1234", "A")
    rows = db.conn.execute("SELECT DISTINCT source FROM events WHERE device_id=?", (acurite,)).fetchall()
    assert {r["source"] for r in rows} == {"dongle-433"}


def test_multi_is_deterministic_in_totals(db, fixtures_dir):
    """Thread interleaving is nondeterministic, but totals must be stable."""
    c = Collector(db, place="home")
    run_multi(c, [
        SourceStream(replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl"), "a"),
        SourceStream(replay_file(fixtures_dir / "rtl433" / "garage_915.jsonl"), "b"),
    ])
    assert c.stats.decoded == 11
    assert db.device_count() == 6
