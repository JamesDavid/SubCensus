"""SQLite catalog (System §4/§9, Pi §5). WAL mode; devices / events / unknowns.

Collector logic: parse each rtl_433 JSON line -> derive device_id from (model,id,channel) ->
upsert `devices` (last_seen/count/avg_snr/typical_freq) -> insert `events`, both stamped with
the active `place`. Undecoded triggers -> `unknowns` (M4). The `place` column is present from
the start (default 'home'); place scoping becomes user-facing in M8.
"""

from __future__ import annotations

import hashlib
import json
import sqlite3
from dataclasses import dataclass
from pathlib import Path

SCHEMA = """
CREATE TABLE IF NOT EXISTS devices(
    device_id TEXT PRIMARY KEY,
    model TEXT, dev_id TEXT, channel TEXT,
    place TEXT,
    first_seen TEXT, last_seen TEXT, count INTEGER,
    typical_freq_hz INTEGER, avg_snr REAL,
    label TEXT, room TEXT, device_class TEXT, notes TEXT
);
CREATE TABLE IF NOT EXISTS events(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT, device_id TEXT, place TEXT, freq_hz INTEGER,
    rssi REAL, snr REAL, source TEXT, raw_json TEXT
);
CREATE TABLE IF NOT EXISTS unknowns(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT, place TEXT, freq_hz INTEGER, source TEXT,
    iq_path TEXT, pulse_summary TEXT,
    label TEXT, device_class TEXT, notes TEXT
);
CREATE INDEX IF NOT EXISTS idx_events_device ON events(device_id);
CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts);
CREATE INDEX IF NOT EXISTS idx_devices_place ON devices(place);
"""


def device_id_for(model: str, dev_id: str, channel: str) -> str:
    """Stable hash of (model, id, channel) — one physical device = one row (Pi §5, §6)."""
    key = f"{model}|{dev_id}|{channel}"
    return hashlib.sha1(key.encode("utf-8")).hexdigest()[:16]


@dataclass
class Reception:
    """A normalized, decoded rtl_433 reception (parser output)."""

    ts: str
    model: str
    dev_id: str
    channel: str
    freq_hz: int
    rssi: float | None
    snr: float | None
    source: str  # dongle/band tag
    place: str
    raw_json: str


class Database:
    def __init__(self, path: str | Path):
        self.path = str(path)
        self.conn = sqlite3.connect(self.path)
        self.conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.execute("PRAGMA foreign_keys=ON")
        self.conn.executescript(SCHEMA)
        self.conn.commit()

    def close(self) -> None:
        self.conn.close()

    # --- writes ---

    def ingest(self, r: Reception) -> str:
        """Upsert the device and insert the event. Returns the device_id."""
        did = device_id_for(r.model, r.dev_id, r.channel)
        cur = self.conn.execute("SELECT count, avg_snr FROM devices WHERE device_id=?", (did,))
        row = cur.fetchone()
        if row is None:
            self.conn.execute(
                "INSERT INTO devices(device_id, model, dev_id, channel, place, first_seen,"
                " last_seen, count, typical_freq_hz, avg_snr, label, room, device_class, notes)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (did, r.model, r.dev_id, r.channel, r.place, r.ts, r.ts, 1,
                 r.freq_hz, r.snr, None, None, None, None),
            )
        else:
            count = row["count"] + 1
            avg_snr = row["avg_snr"]
            if r.snr is not None:
                avg_snr = r.snr if avg_snr is None else (avg_snr * row["count"] + r.snr) / count
            self.conn.execute(
                "UPDATE devices SET last_seen=?, count=?, typical_freq_hz=?, avg_snr=?"
                " WHERE device_id=?",
                (r.ts, count, r.freq_hz, avg_snr, did),
            )
        self.conn.execute(
            "INSERT INTO events(ts, device_id, place, freq_hz, rssi, snr, source, raw_json)"
            " VALUES(?,?,?,?,?,?,?,?)",
            (r.ts, did, r.place, r.freq_hz, r.rssi, r.snr, r.source, r.raw_json),
        )
        self.conn.commit()
        return did

    def insert_unknown(
        self, ts: str, place: str, freq_hz: int, source: str,
        iq_path: str | None = None, pulse_summary: str | None = None,
    ) -> int:
        cur = self.conn.execute(
            "INSERT INTO unknowns(ts, place, freq_hz, source, iq_path, pulse_summary,"
            " label, device_class, notes) VALUES(?,?,?,?,?,?,?,?,?)",
            (ts, place, freq_hz, source, iq_path, pulse_summary, None, None, None),
        )
        self.conn.commit()
        return cur.lastrowid

    def set_device_label(
        self, device_id: str, label: str | None = None, room: str | None = None,
        device_class: str | None = None, notes: str | None = None,
    ) -> None:
        """Manual labeling (dashboard §7). Writes straight to SQLite (Pi §6)."""
        self.conn.execute(
            "UPDATE devices SET label=COALESCE(?, label), room=COALESCE(?, room),"
            " device_class=COALESCE(?, device_class), notes=COALESCE(?, notes)"
            " WHERE device_id=?",
            (label, room, device_class, notes, device_id),
        )
        self.conn.commit()

    # --- reads ---

    def distinct_places(self) -> list[str]:
        rows = self.conn.execute(
            "SELECT DISTINCT place FROM devices WHERE place IS NOT NULL AND place<>''"
            " ORDER BY place"
        ).fetchall()
        return [r["place"] for r in rows]

    def device_count(self, place: str | None = None) -> int:
        if place:
            return self.conn.execute(
                "SELECT COUNT(*) c FROM devices WHERE place=?", (place,)
            ).fetchone()["c"]
        return self.conn.execute("SELECT COUNT(*) c FROM devices").fetchone()["c"]

    def event_count(self) -> int:
        return self.conn.execute("SELECT COUNT(*) c FROM events").fetchone()["c"]

    def get_device(self, device_id: str) -> sqlite3.Row | None:
        return self.conn.execute("SELECT * FROM devices WHERE device_id=?", (device_id,)).fetchone()

    def list_devices(self, place: str | None = None) -> list[sqlite3.Row]:
        if place:
            return self.conn.execute(
                "SELECT * FROM devices WHERE place=? ORDER BY last_seen DESC", (place,)
            ).fetchall()
        return self.conn.execute("SELECT * FROM devices ORDER BY last_seen DESC").fetchall()

    def recent_events(self, limit: int = 50, place: str | None = None) -> list[sqlite3.Row]:
        if place:
            return self.conn.execute(
                "SELECT e.*, d.model, d.label, d.room FROM events e"
                " LEFT JOIN devices d ON e.device_id=d.device_id"
                " WHERE e.place=? ORDER BY e.id DESC LIMIT ?", (place, limit)
            ).fetchall()
        return self.conn.execute(
            "SELECT e.*, d.model, d.label, d.room FROM events e"
            " LEFT JOIN devices d ON e.device_id=d.device_id"
            " ORDER BY e.id DESC LIMIT ?", (limit,)
        ).fetchall()

    # --- unknowns (review queue, Pi §6) ---

    def list_unknowns(self, place: str | None = None) -> list[sqlite3.Row]:
        if place:
            return self.conn.execute(
                "SELECT * FROM unknowns WHERE place=? ORDER BY id DESC", (place,)
            ).fetchall()
        return self.conn.execute("SELECT * FROM unknowns ORDER BY id DESC").fetchall()

    def get_unknown(self, unknown_id: int) -> sqlite3.Row | None:
        return self.conn.execute("SELECT * FROM unknowns WHERE id=?", (unknown_id,)).fetchone()

    def set_unknown_label(
        self, unknown_id: int, device_class: str | None = None, notes: str | None = None,
        label: str | None = None,
    ) -> None:
        self.conn.execute(
            "UPDATE unknowns SET device_class=COALESCE(?, device_class),"
            " notes=COALESCE(?, notes), label=COALESCE(?, label) WHERE id=?",
            (device_class, notes, label, unknown_id),
        )
        self.conn.commit()

    def delete_unknown(self, unknown_id: int) -> bool:
        cur = self.conn.execute("DELETE FROM unknowns WHERE id=?", (unknown_id,))
        self.conn.commit()
        return cur.rowcount > 0

    def unknown_count(self) -> int:
        return self.conn.execute("SELECT COUNT(*) c FROM unknowns").fetchone()["c"]

    def device_event_timestamps(self, device_id: str) -> list[str]:
        rows = self.conn.execute(
            "SELECT ts FROM events WHERE device_id=? ORDER BY ts", (device_id,)
        ).fetchall()
        return [r["ts"] for r in rows]
