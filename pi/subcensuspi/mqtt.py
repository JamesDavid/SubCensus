"""MQTT -> Home Assistant auto-discovery (Pi §9).

The collector republishes catalogued devices as HA MQTT-discovery entities so found devices
show up as entities automatically (fits an existing HA/MQTT setup). Discovery configs are
retained; state is published per reception.

The paho client is injectable, so tests drive a fake broker (payload assertions) with no
mosquitto (Debug §4). Live broker connectivity is the only part that needs a real broker.
"""

from __future__ import annotations

import json
import logging
from typing import Any

from .config import MqttConfig

log = logging.getLogger("subcensuspi.mqtt")

HA_PREFIX = "homeassistant"


def device_identifier(base_topic: str, device_id: str) -> str:
    return f"{base_topic}_{device_id}"


def state_topic(base_topic: str, device_id: str) -> str:
    return f"{base_topic}/device/{device_id}/state"


def _device_block(base_topic: str, device: dict) -> dict:
    name = device.get("label") or device.get("model") or "SubCensus device"
    return {
        "identifiers": [device_identifier(base_topic, device["device_id"])],
        "name": name,
        "model": device.get("model") or "unknown",
        "manufacturer": "SubCensusPi",
    }


def build_discovery_configs(device: dict, base_topic: str) -> list[tuple[str, dict]]:
    """Return (config_topic, payload) pairs registering this device's HA entities."""
    did = device["device_id"]
    st = state_topic(base_topic, did)
    dev_block = _device_block(base_topic, device)
    name = device.get("label") or device.get("model") or did

    def entity(key: str, label: str, template: str, *, unit=None, ha_class=None) -> tuple[str, dict]:
        payload: dict[str, Any] = {
            "name": f"{name} {label}",
            "state_topic": st,
            "value_template": template,
            "unique_id": f"{device_identifier(base_topic, did)}_{key}",
            "device": dev_block,
        }
        if unit:
            payload["unit_of_measurement"] = unit
        if ha_class:
            payload["device_class"] = ha_class
        topic = f"{HA_PREFIX}/sensor/{base_topic}/{did}_{key}/config"
        return topic, payload

    return [
        entity("rssi", "RSSI", "{{ value_json.rssi }}", unit="dBm", ha_class="signal_strength"),
        entity("last_seen", "Last seen", "{{ value_json.last_seen }}", ha_class="timestamp"),
        entity("count", "Count", "{{ value_json.count }}"),
    ]


def build_state_payload(device: dict, event: dict | None = None) -> dict:
    return {
        "rssi": (event or {}).get("rssi", device.get("avg_snr")),
        "last_seen": device.get("last_seen"),
        "count": device.get("count"),
        "freq_hz": device.get("typical_freq_hz"),
    }


class MqttPublisher:
    """Thin wrapper over paho-mqtt with an injectable client (tests pass a fake)."""

    def __init__(self, config: MqttConfig, client: Any = None):
        self.cfg = config
        self._connected = False
        if client is not None:
            self.client = client
        else:  # pragma: no cover - exercised only with a real broker
            import paho.mqtt.client as mqtt

            self.client = mqtt.Client()

    def connect(self) -> None:
        self.client.connect(self.cfg.host, self.cfg.port)
        self._connected = True

    def disconnect(self) -> None:
        if self._connected:
            self.client.disconnect()
            self._connected = False

    def publish_discovery(self, device: dict) -> int:
        """Publish (retained) HA discovery configs for a device. Returns entity count."""
        if not self.cfg.ha_discovery:
            return 0
        n = 0
        for topic, payload in build_discovery_configs(device, self.cfg.base_topic):
            self.client.publish(topic, json.dumps(payload), retain=True)
            n += 1
        return n

    def publish_state(self, device: dict, event: dict | None = None) -> None:
        payload = build_state_payload(device, event)
        self.client.publish(
            state_topic(self.cfg.base_topic, device["device_id"]),
            json.dumps(payload),
            retain=False,
        )
