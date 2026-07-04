"""Shared signature-DB (the classification brain) IO + merge (System §6, §8).

protocol_map.csv (decoded -> name/class) + fingerprints.csv (feature-vector library) are the
GLOBAL brain, shared across places and both tools. This module reads/writes them in the shared
schema and merges labeled fingerprints from BOTH tools with dedup — the single merge point
build_signatures.py drives (System §8). Proposes/merges; never auto-relabels (System §6).
"""

from __future__ import annotations

import csv
from pathlib import Path

from .schema import Schema, load_all_schemas

_SCHEMAS: dict[str, Schema] = {}


def _schema(name: str) -> Schema:
    if not _SCHEMAS:
        _SCHEMAS.update(load_all_schemas())
    return _SCHEMAS[name]


def _read_csv(path: Path, schema: Schema) -> list[dict]:
    if not path.exists():
        return []
    with path.open("r", newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    header = set(schema.header())
    return [{k: (r.get(k) or "") for k in header} for r in rows]


def _write_csv(rows: list[dict], path: Path, schema: Schema) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    cols = schema.header()
    with path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(cols)
        for r in rows:
            w.writerow([r.get(c, "") for c in cols])


def read_fingerprints(path: str | Path) -> list[dict]:
    return _read_csv(Path(path), _schema("fingerprints"))


def write_fingerprints(rows: list[dict], path: str | Path) -> None:
    # `id` is required (schema); assign a stable provisional id to any row missing one so
    # every written fingerprints.csv is schema-valid (merge re-ids canonically).
    out = []
    for i, r in enumerate(rows):
        r = dict(r)
        if not r.get("id"):
            r["id"] = f"fp{i:05d}"
        out.append(r)
    _write_csv(out, Path(path), _schema("fingerprints"))


def read_protocol_map(path: str | Path) -> list[dict]:
    return _read_csv(Path(path), _schema("protocol_map"))


def write_protocol_map(rows: list[dict], path: str | Path) -> None:
    _write_csv(rows, Path(path), _schema("protocol_map"))


# --- merge / dedup ---

_SOURCE_PRIORITY = {"user": 3, "tool": 2, "seed": 1, "": 0}

_FP_KEY_COLS = (
    "freq_bin", "modulation", "sym_dur_us_1", "sym_dur_us_2", "sym_dur_us_3",
    "n_symbols", "est_bitrate", "preamble_len", "repeat_count", "device_class",
)


def _fp_key(row: dict) -> tuple:
    return tuple(str(row.get(c, "")) for c in _FP_KEY_COLS)


def merge_fingerprints(sources: list[list[dict]]) -> list[dict]:
    """Merge fingerprint rows from multiple tools/places; dedup identical vectors, keeping
    the highest-provenance one (user > tool > seed). Re-ids sequentially and stably."""
    best: dict[tuple, dict] = {}
    for rows in sources:
        for row in rows:
            key = _fp_key(row)
            cur = best.get(key)
            if cur is None or _SOURCE_PRIORITY.get(row.get("source", ""), 0) > _SOURCE_PRIORITY.get(
                cur.get("source", ""), 0
            ):
                best[key] = dict(row)
    merged = sorted(best.values(), key=_fp_key)
    for i, row in enumerate(merged):
        row["id"] = f"fp{i:05d}"
    return merged


def merge_protocol_map(sources: list[list[dict]]) -> list[dict]:
    """Merge protocol_map rows keyed by `protocol`; later (higher-priority) sources win on
    conflict but never blank an existing friendly_name/device_class."""
    by_proto: dict[str, dict] = {}
    for rows in sources:
        for row in rows:
            proto = row.get("protocol", "")
            if not proto:
                continue
            if proto not in by_proto:
                by_proto[proto] = dict(row)
            else:
                for k, v in row.items():
                    if v:
                        by_proto[proto][k] = v
    return sorted(by_proto.values(), key=lambda r: r["protocol"])


def lookup_protocol(protocol_map: list[dict], protocol: str) -> dict | None:
    """Tier-1 classification (System §6): decoded protocol -> friendly name + class."""
    for row in protocol_map:
        if row.get("protocol") == protocol:
            return row
    return None
