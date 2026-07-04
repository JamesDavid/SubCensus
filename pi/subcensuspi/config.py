"""YAML config (Pi §8)."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import yaml


@dataclass
class DongleConfig:
    serial: str = ""
    freqs: list[str] = field(default_factory=lambda: ["433.92M"])
    gain: int | str = "auto"
    ppm: int = 0
    hop_seconds: int = 30


@dataclass
class MqttConfig:
    enabled: bool = False
    host: str = "127.0.0.1"
    port: int = 1883
    ha_discovery: bool = True
    base_topic: str = "subcensuspi"


@dataclass
class WebConfig:
    host: str = "0.0.0.0"
    port: int = 8080


@dataclass
class Config:
    dongles: list[DongleConfig] = field(default_factory=lambda: [DongleConfig()])
    capture_unknowns: bool = False
    place: str = "home"
    places_dir: str = "/var/lib/subcensuspi/places"
    signatures_dir: str = "/var/lib/subcensuspi/signatures"
    iq_dir: str = "/var/lib/subcensuspi/iq"
    max_iq_gb: int = 20
    db_path: str = "/var/lib/subcensuspi/census.db"
    mqtt: MqttConfig = field(default_factory=MqttConfig)
    web: WebConfig = field(default_factory=WebConfig)

    @classmethod
    def load(cls, path: str | Path) -> "Config":
        data = yaml.safe_load(Path(path).read_text(encoding="utf-8")) or {}
        return cls.from_dict(data)

    @classmethod
    def from_dict(cls, data: dict) -> "Config":
        dongles = [
            DongleConfig(
                serial=str(d.get("serial", "")),
                freqs=[str(f) for f in d.get("freqs", ["433.92M"])],
                gain=d.get("gain", "auto"),
                ppm=int(d.get("ppm", 0)),
                hop_seconds=int(d.get("hop_seconds", 30)),
            )
            for d in data.get("dongles", [{}])
        ]
        mqtt = data.get("mqtt", {})
        web = data.get("web", {})
        return cls(
            dongles=dongles,
            capture_unknowns=bool(data.get("capture_unknowns", False)),
            place=str(data.get("place", "home")),
            places_dir=str(data.get("places_dir", "/var/lib/subcensuspi/places")),
            signatures_dir=str(data.get("signatures_dir", "/var/lib/subcensuspi/signatures")),
            iq_dir=str(data.get("iq_dir", "/var/lib/subcensuspi/iq")),
            max_iq_gb=int(data.get("max_iq_gb", 20)),
            db_path=str(data.get("db_path", "/var/lib/subcensuspi/census.db")),
            mqtt=MqttConfig(
                enabled=bool(mqtt.get("enabled", False)),
                host=str(mqtt.get("host", "127.0.0.1")),
                port=int(mqtt.get("port", 1883)),
                ha_discovery=bool(mqtt.get("ha_discovery", True)),
                base_topic=str(mqtt.get("base_topic", "subcensuspi")),
            ),
            web=WebConfig(
                host=str(web.get("host", "0.0.0.0")),
                port=int(web.get("port", 8080)),
            ),
        )
