"""Loader + validator for shared/schema/*.schema.yaml (System §7, §9).

These specs are the single source of truth for every shared CSV. This module loads them,
exposes the exact header order, and validates any CSV (from either tool) column-for-column
and cell-by-cell against types/enums/taxonomy — so a place folder from the Zero and one
from the Pi are provably interchangeable.
"""

from __future__ import annotations

import csv as _csv
import re
from dataclasses import dataclass, field
from pathlib import Path

import yaml

from .paths import repo_paths
from .taxonomy import Taxonomy

SCALAR_TYPES = {"int", "float", "str", "bool", "ts", "enum", "device_class"}

_TS_RE = re.compile(r"^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}")  # lenient ISO-8601 prefix


@dataclass(frozen=True)
class Column:
    name: str
    type: str
    required: bool = False
    values: tuple[str, ...] = ()
    desc: str = ""


@dataclass(frozen=True)
class Schema:
    name: str
    description: str
    system_ref: str
    scope: str
    columns: tuple[Column, ...] = field(default_factory=tuple)

    def header(self) -> list[str]:
        return [c.name for c in self.columns]

    def column(self, name: str) -> Column | None:
        for c in self.columns:
            if c.name == name:
                return c
        return None

    @classmethod
    def from_dict(cls, data: dict) -> "Schema":
        for key in ("name", "columns"):
            if key not in data:
                raise ValueError(f"schema missing required key {key!r}")
        cols: list[Column] = []
        seen: set[str] = set()
        for i, c in enumerate(data["columns"]):
            if "name" not in c or "type" not in c:
                raise ValueError(f"{data['name']}: column #{i} needs 'name' and 'type'")
            ctype = str(c["type"])
            if ctype not in SCALAR_TYPES:
                raise ValueError(f"{data['name']}.{c['name']}: unknown type {ctype!r}")
            values = tuple(str(v) for v in c.get("values", []))
            if ctype == "enum" and not values:
                raise ValueError(f"{data['name']}.{c['name']}: enum needs 'values'")
            name = str(c["name"])
            if name in seen:
                raise ValueError(f"{data['name']}: duplicate column {name!r}")
            seen.add(name)
            cols.append(
                Column(
                    name=name,
                    type=ctype,
                    required=bool(c.get("required", False)),
                    values=values,
                    desc=str(c.get("desc", "")),
                )
            )
        return cls(
            name=str(data["name"]),
            description=str(data.get("description", "")),
            system_ref=str(data.get("system_ref", "")),
            scope=str(data.get("scope", "")),
            columns=tuple(cols),
        )

    @classmethod
    def load(cls, path: Path) -> "Schema":
        return cls.from_dict(yaml.safe_load(Path(path).read_text(encoding="utf-8")))


def load_all_schemas(schema_dir: Path | None = None) -> dict[str, Schema]:
    d = Path(schema_dir or repo_paths()["schema_dir"])
    out: dict[str, Schema] = {}
    for f in sorted(d.glob("*.schema.yaml")):
        s = Schema.load(f)
        out[s.name] = s
    return out


# --- validation -----------------------------------------------------------------

def _cell_errors(col: Column, value: str, taxonomy: Taxonomy) -> str | None:
    """Return an error string for a single cell, or None if valid."""
    v = value.strip()
    if v == "":
        return f"{col.name}: required but empty" if col.required else None
    if col.type == "int":
        try:
            int(v)
        except ValueError:
            return f"{col.name}: {v!r} is not an int"
    elif col.type == "float":
        try:
            float(v)
        except ValueError:
            return f"{col.name}: {v!r} is not a float"
    elif col.type == "bool":
        if v not in ("0", "1"):
            return f"{col.name}: {v!r} is not a bool (expected 0/1)"
    elif col.type == "ts":
        if not _TS_RE.match(v):
            return f"{col.name}: {v!r} is not an ISO-8601 timestamp"
    elif col.type == "enum":
        if v not in col.values:
            return f"{col.name}: {v!r} not in {list(col.values)}"
    elif col.type == "device_class":
        if not taxonomy.is_valid(v):
            return f"{col.name}: {v!r} is not a known device_class"
    # str: anything non-empty is fine
    return None


def validate_row(schema: Schema, row: dict[str, str], taxonomy: Taxonomy) -> list[str]:
    errors: list[str] = []
    expected = set(schema.header())
    extra = set(row) - expected
    if extra:
        errors.append(f"unexpected columns: {sorted(extra)}")
    for col in schema.columns:
        if col.name not in row:
            errors.append(f"{col.name}: missing column")
            continue
        err = _cell_errors(col, row[col.name] or "", taxonomy)
        if err:
            errors.append(err)
    return errors


def validate_csv(
    schema: Schema, path: Path, taxonomy: Taxonomy | None = None
) -> list[str]:
    """Validate a CSV against a schema. Returns a flat list of error strings ([] = ok)."""
    tax = taxonomy or Taxonomy.load()
    errors: list[str] = []
    with Path(path).open("r", newline="", encoding="utf-8") as fh:
        reader = _csv.reader(fh)
        try:
            header = next(reader)
        except StopIteration:
            return [f"{path.name}: file is empty (no header)"]
        if header != schema.header():
            return [
                f"{path.name}: header mismatch\n"
                f"  expected: {schema.header()}\n"
                f"  found:    {header}"
            ]
        for lineno, raw in enumerate(reader, start=2):
            if len(raw) != len(header):
                errors.append(f"{path.name}:{lineno}: expected {len(header)} cells, got {len(raw)}")
                continue
            row = dict(zip(header, raw))
            for e in validate_row(schema, row, tax):
                errors.append(f"{path.name}:{lineno}: {e}")
    return errors
