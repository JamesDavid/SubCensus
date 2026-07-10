"""Collector: rtl_433 JSON stream -> SQLite catalog (Pi §2, §5).

Decoupled from the rtl_433 process so the full decode -> roll-up -> SQLite path is driven
deterministically from recorded JSON in tests (Debug §4) — no dongle, no rtl_433 binary.
"""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass
from typing import Iterable

from ..db import Database
from ..mqtt import MqttPublisher
from .parser import parse_line
from .unknowns import disk_guard_ok, pulse_summary_from_event

log = logging.getLogger("subcensuspi.collector")


@dataclass
class CollectorStats:
    lines: int = 0
    decoded: int = 0
    unknowns: int = 0
    unknowns_dropped: int = 0  # skipped by the disk guard (Pi §4)
    skipped: int = 0


class Collector:
    def __init__(
        self,
        db: Database,
        place: str = "home",
        capture_unknowns: bool = False,
        iq_dir: str = "",
        max_iq_gb: float = 20,
        mqtt: MqttPublisher | None = None,
        brain=None,
    ):
        self.db = db
        self.place = place
        self.capture_unknowns = capture_unknowns
        self.iq_dir = iq_dir
        self.max_iq_gb = max_iq_gb
        self.mqtt = mqtt
        self.brain = brain  # classification brain (System §6): protocol_map lookup on each decode
        self._mqtt_announced: set[str] = set()  # devices whose HA discovery config was sent
        self.stats = CollectorStats()

    def process_line(self, line: str, source: str = "") -> str | None:
        """Process one JSON line. Returns the device_id if a decoded event, else None."""
        self.stats.lines += 1
        r = parse_line(line, source=source, place=self.place)
        if r is not None:
            did = self.db.ingest(r)
            self.stats.decoded += 1
            # Classification brain (System §6.1): propose friendly name + device_class from
            # protocol_map. Advisory — written to match_* only, never overwrites a user label.
            if self.brain is not None:
                m = self.brain.classify(r.model)
                if m is not None:
                    self.db.set_device_match(
                        did, m["name"], m["device_class"], m["confidence"], m["source"]
                    )
            log.info(
                "SC event=decoded model=%s id=%s freq=%d rssi=%s device=%s",
                r.model, r.dev_id, r.freq_hz, r.rssi, did,
            )
            self._publish_mqtt(did, r)
            return did
        # non-decoded line: an unknown-capture trigger, or a stats/protocol line we ignore
        if self.capture_unknowns:
            obj = _safe_json(line)
            if obj is not None and _looks_like_unknown(obj):
                # disk guard: stop capturing unknowns past max_iq_gb (Pi §4)
                if self.iq_dir and not disk_guard_ok(self.iq_dir, self.max_iq_gb):
                    self.stats.unknowns_dropped += 1
                    log.warning("SC event=unknown_dropped reason=disk_full iq_dir=%s", self.iq_dir)
                    return None
                freq_hz = (
                    int(round(float(obj.get("freq", 0)) * 1_000_000)) if obj.get("freq") else 0
                )
                self.db.insert_unknown(
                    ts=str(obj.get("time", "")),
                    place=self.place,
                    freq_hz=freq_hz,
                    source=source,
                    pulse_summary=pulse_summary_from_event(obj),
                )
                self.stats.unknowns += 1
                log.info("SC event=unknown freq=%d source=%s", freq_hz, source)
                return None
        self.stats.skipped += 1
        return None

    def _publish_mqtt(self, device_id: str, reception) -> None:
        """Republish a decoded reception to Home Assistant via MQTT discovery (Pi §9).

        Discovery config is published once per device (retained); state is published every
        reception. The device row (rolled-up first/last-seen, count, avg_snr, label) is the
        source of truth for the HA entity. The publisher is injectable, so a fake broker
        exercises this whole path with no mosquitto; only the socket connect needs a live
        broker (TODO(hw))."""
        if self.mqtt is None:
            return
        row = self.db.get_device(device_id)
        if row is None:
            return
        device = dict(row)
        if device_id not in self._mqtt_announced:
            self.mqtt.publish_discovery(device)
            self._mqtt_announced.add(device_id)
        self.mqtt.publish_state(device, {"rssi": reception.rssi})

    def process_stream(self, lines: Iterable[str], source: str = "") -> CollectorStats:
        for line in lines:
            self.process_line(line, source=source)
        return self.stats


def _safe_json(line: str) -> dict | None:
    try:
        obj = json.loads(line)
        return obj if isinstance(obj, dict) else None
    except json.JSONDecodeError:
        return None


def _looks_like_unknown(obj: dict) -> bool:
    # rtl_433 -A pulse-analyzer / -S trigger output carries pulse info but no model
    return "model" not in obj and ("pulses" in obj or "mod" in obj or "codes" in obj)
