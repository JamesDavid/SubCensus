"""M5: MQTT / Home Assistant discovery (Pi §9). Fake broker — no mosquitto needed."""

import json

from subcensuspi.config import MqttConfig
from subcensuspi.mqtt import (
    HA_PREFIX,
    MqttPublisher,
    build_discovery_configs,
    build_state_payload,
    state_topic,
)


class FakeClient:
    def __init__(self):
        self.published = []  # (topic, payload, retain)
        self.connected = False

    def connect(self, host, port):
        self.connected = True

    def disconnect(self):
        self.connected = False

    def publish(self, topic, payload, retain=False):
        self.published.append((topic, payload, retain))


DEVICE = {
    "device_id": "abc123",
    "model": "Acurite-Tower",
    "label": "Garden temp",
    "avg_snr": 12.0,
    "last_seen": "2026-07-04T12:03:00",
    "count": 4,
    "typical_freq_hz": 433920000,
}


def test_discovery_config_structure():
    configs = build_discovery_configs(DEVICE, "subcensuspi")
    topics = [t for t, _ in configs]
    assert f"{HA_PREFIX}/sensor/subcensuspi/abc123_rssi/config" in topics
    rssi = next(p for t, p in configs if t.endswith("_rssi/config"))
    assert rssi["unique_id"] == "subcensuspi_abc123_rssi"
    assert rssi["device_class"] == "signal_strength"
    assert rssi["state_topic"] == state_topic("subcensuspi", "abc123")
    assert rssi["device"]["identifiers"] == ["subcensuspi_abc123"]
    assert rssi["device"]["name"] == "Garden temp"  # label wins over model


def test_state_payload():
    p = build_state_payload(DEVICE, {"rssi": -60.5})
    assert p["rssi"] == -60.5
    assert p["last_seen"] == "2026-07-04T12:03:00"
    assert p["count"] == 4


def test_publisher_with_fake_broker():
    fake = FakeClient()
    pub = MqttPublisher(MqttConfig(enabled=True, ha_discovery=True, base_topic="subcensuspi"), client=fake)
    pub.connect()
    assert fake.connected
    n = pub.publish_discovery(DEVICE)
    assert n == 3  # rssi, last_seen, count
    # discovery configs are retained
    assert all(retain for _, _, retain in fake.published)
    pub.publish_state(DEVICE, {"rssi": -60.5})
    state = fake.published[-1]
    assert state[0] == state_topic("subcensuspi", "abc123")
    assert json.loads(state[1])["rssi"] == -60.5
    assert state[2] is False  # state is not retained
    pub.disconnect()
    assert not fake.connected


def test_discovery_disabled():
    fake = FakeClient()
    pub = MqttPublisher(MqttConfig(enabled=True, ha_discovery=False), client=fake)
    assert pub.publish_discovery(DEVICE) == 0
    assert fake.published == []


def test_collector_publishes_to_mqtt_via_fake_broker(tmp_path, fixtures_dir):
    """A1: the collector ingest path republishes to HA over MQTT (Pi §9). Wired end-to-end
    against a fake broker — only the socket connect needs a real broker (TODO(hw))."""
    from subcensuspi.collector.collector import Collector
    from subcensuspi.collector.rtl433 import replay_file
    from subcensuspi.db import Database

    fake = FakeClient()
    pub = MqttPublisher(MqttConfig(enabled=True, ha_discovery=True, base_topic="subcensuspi"),
                        client=fake)
    pub.connect()
    db = Database(tmp_path / "census.db")
    c = Collector(db, place="home", mqtt=pub)
    c.process_stream(replay_file(fixtures_dir / "rtl433" / "home_stream.jsonl"))

    # discovery is published once per distinct device (retained); state every reception (not).
    discovery = [p for p in fake.published if p[0].startswith(HA_PREFIX)]
    state = [p for p in fake.published if "/device/" in p[0] and p[0].endswith("/state")]
    assert len(state) == 7  # 7 decoded receptions in the fixture
    assert all(retain for _, _, retain in discovery)
    assert all(not retain for _, _, retain in state)
    # 4 distinct devices x 3 entities each = 12 discovery configs; no dup announces
    assert len({t for t, _, _ in discovery}) == 12
    # state carries the live rssi and the rolled-up device fields
    last = json.loads(state[-1][1])
    assert "rssi" in last and "count" in last and "last_seen" in last
    db.close()
