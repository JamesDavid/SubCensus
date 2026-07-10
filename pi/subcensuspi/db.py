"""SQLite catalog (System §4/§9, Pi §5). WAL mode; devices / events / unknowns.

Collector logic: parse each rtl_433 JSON line -> derive device_id from (model,id,channel) ->
upsert `devices` (last_seen/count/avg_snr/typical_freq) -> insert `events`, both stamped with
the active `place`. Undecoded triggers -> `unknowns` (M4). The `place` column is present from
the start (default 'home'); place scoping becomes user-facing in M8.
"""

from __future__ import annotations

import datetime as _dt
import hashlib
import json
import sqlite3
from dataclasses import dataclass
from pathlib import Path

from .dsp import cadence as _cadence

# cadence_* columns added after the original devices table shipped — ALTER in for old DBs.
_DEVICE_CADENCE_COLS = {
    "cadence_class": "TEXT",
    "period_s": "REAL",
    "period_regularity": "REAL",
    "cadence_samples": "INTEGER",
}


def iso_to_epoch(ts: str) -> float | None:
    """Best-effort ISO-8601 -> epoch seconds (rtl_433 -M time:iso:tz). None if unparseable."""
    try:
        return _dt.datetime.fromisoformat(ts).timestamp()
    except (TypeError, ValueError):
        return None

SCHEMA = """
CREATE TABLE IF NOT EXISTS devices(
    device_id TEXT PRIMARY KEY,
    model TEXT, dev_id TEXT, channel TEXT,
    place TEXT,
    first_seen TEXT, last_seen TEXT, count INTEGER,
    typical_freq_hz INTEGER, avg_snr REAL,
    label TEXT, room TEXT, device_class TEXT, notes TEXT,
    -- dropout-robust cadence (System §7a); the Pi is the strongest measurer (Pi §5).
    cadence_class TEXT, period_s REAL, period_regularity REAL, cadence_samples INTEGER
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
        # sqlite won't create missing parent dirs — do it ourselves so a fresh install (or a
        # custom db_path) Just Works instead of failing with "unable to open database file".
        parent = Path(self.path).parent
        if str(parent) not in ("", "."):
            try:
                parent.mkdir(parents=True, exist_ok=True)
            except OSError as e:
                raise RuntimeError(
                    f"SubCensusPi: cannot create the database directory {parent} "
                    f"({e.strerror}). Run pi/install.sh (it provisions "
                    f"/var/lib/subcensuspi owned by you), or set db_path / --db to a "
                    f"writable path."
                ) from e
        try:
            self.conn = sqlite3.connect(self.path)
        except sqlite3.OperationalError as e:
            raise RuntimeError(
                f"SubCensusPi: cannot open the database {self.path!r} ({e}). The parent "
                f"directory may be unwritable — see pi/install.sh or use --db <writable path>."
            ) from e
        self.conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.execute("PRAGMA foreign_keys=ON")
        self.conn.executescript(SCHEMA)
        self._migrate()
        self.conn.commit()

    def _migrate(self) -> None:
        """Additive migrations for DBs created before newer columns (idempotent)."""
        have = {r["name"] for r in self.conn.execute("PRAGMA table_info(devices)").fetchall()}
        for col, coltype in _DEVICE_CADENCE_COLS.items():
            if col not in have:
                self.conn.execute(f"ALTER TABLE devices ADD COLUMN {col} {coltype}")

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

    def latest_raw_json_by_device(self, place: str | None = None) -> dict[str, str]:
        """The most recent event's raw rtl_433 JSON for each device (one query). Drives the
        Devices "Latest reading" column (Pi §7) — the human-readable decoded payload per device."""
        sql = (
            "SELECT e.device_id AS did, e.raw_json AS raw_json FROM events e"
            " JOIN (SELECT device_id, MAX(id) AS mid FROM events{where} GROUP BY device_id) m"
            " ON e.id = m.mid"
        )
        if place:
            rows = self.conn.execute(
                sql.format(where=" WHERE place=?"), (place,)
            ).fetchall()
        else:
            rows = self.conn.execute(sql.format(where="")).fetchall()
        return {r["did"]: r["raw_json"] for r in rows if r["raw_json"]}

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

    def device_events(self, device_id: str) -> list[sqlite3.Row]:
        """All events for a device in reception order (for the differential corpus, §7b)."""
        return self.conn.execute(
            "SELECT * FROM events WHERE device_id=? ORDER BY id", (device_id,)
        ).fetchall()

    def unknown_timestamps(self, place: str, freq_hz: int) -> list[str]:
        """Capture times of every unknown burst at a (place, freq) — the reception history a
        repeatedly-heard unknown signal accrues (feeds its cadence, §7a)."""
        rows = self.conn.execute(
            "SELECT ts FROM unknowns WHERE place=? AND freq_hz=? ORDER BY ts", (place, freq_hz)
        ).fetchall()
        return [r["ts"] for r in rows]

    # --- cadence (System §7a; the Pi is the strongest measurer, Pi §5) ---

    def device_cadence(self, device_id: str) -> _cadence.CadenceEstimate:
        """Dropout-robust cadence from this device's full event history (ISO ts -> epoch)."""
        secs = [e for e in (iso_to_epoch(t) for t in self.device_event_timestamps(device_id))
                if e is not None]
        return _cadence.from_timestamps(secs)

    def update_device_cadence(self, device_id: str) -> _cadence.CadenceEstimate:
        """Recompute + persist the four cadence_* columns for a device. period_s NULL when the
        cadence class has no fundamental (event-driven/seen-once)."""
        est = self.device_cadence(device_id)
        period = est.period_s if est.period_s > 0 else None
        self.conn.execute(
            "UPDATE devices SET cadence_class=?, period_s=?, period_regularity=?,"
            " cadence_samples=? WHERE device_id=?",
            (est.cls, period, est.regularity, est.samples, device_id),
        )
        self.conn.commit()
        return est

    def refresh_all_cadences(self) -> int:
        """Recompute cadence for every device (catalog enrichment, §5/§7a). Returns count."""
        ids = [r["device_id"] for r in
               self.conn.execute("SELECT device_id FROM devices").fetchall()]
        for did in ids:
            self.update_device_cadence(did)
        return len(ids)
