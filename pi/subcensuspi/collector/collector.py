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
from .parser import parse_line

log = logging.getLogger("subcensuspi.collector")


@dataclass
class CollectorStats:
    lines: int = 0
    decoded: int = 0
    unknowns: int = 0
    skipped: int = 0


class Collector:
    def __init__(self, db: Database, place: str = "home", capture_unknowns: bool = False):
        self.db = db
        self.place = place
        self.capture_unknowns = capture_unknowns
        self.stats = CollectorStats()

    def process_line(self, line: str, source: str = "") -> str | None:
        """Process one JSON line. Returns the device_id if a decoded event, else None."""
        self.stats.lines += 1
        r = parse_line(line, source=source, place=self.place)
        if r is not None:
            did = self.db.ingest(r)
            self.stats.decoded += 1
            log.info(
                "SC event=decoded model=%s id=%s freq=%d rssi=%s device=%s",
                r.model, r.dev_id, r.freq_hz, r.rssi, did,
            )
            return did
        # non-decoded line: an unknown-capture trigger, or a stats/protocol line we ignore
        if self.capture_unknowns:
            obj = _safe_json(line)
            if obj is not None and _looks_like_unknown(obj):
                self.db.insert_unknown(
                    ts=str(obj.get("time", "")),
                    place=self.place,
                    freq_hz=int(round(float(obj.get("freq", 0)) * 1_000_000)) if obj.get("freq") else 0,
                    source=source,
                    pulse_summary=json.dumps(obj.get("pulses", obj), separators=(",", ":")),
                )
                self.stats.unknowns += 1
                return None
        self.stats.skipped += 1
        return None

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
