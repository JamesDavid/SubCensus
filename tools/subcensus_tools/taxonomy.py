"""Loader for shared/taxonomy.yaml (System §5) — the device_class vocabulary.

This is the authoritative reader used by the codegen and every validator. The `id`
strings are the durable on-disk identity; list order defines the generated C enum values.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

import yaml

from .paths import repo_paths

# Required sentinel classes (System §5) — must always be present.
REQUIRED_IDS = ("unknown", "other")

_ID_RE = re.compile(r"^[a-z0-9]+(?:[-/][a-z0-9]+)*$")


@dataclass(frozen=True)
class DeviceClass:
    id: str
    name: str
    notes: str = ""
    deprecated: bool = False

    @property
    def enum_name(self) -> str:
        return c_enum_name(self.id)


@dataclass(frozen=True)
class Taxonomy:
    version: int
    classes: tuple[DeviceClass, ...] = field(default_factory=tuple)

    def ids(self) -> list[str]:
        return [c.id for c in self.classes]

    def active(self) -> list[DeviceClass]:
        return [c for c in self.classes if not c.deprecated]

    def is_valid(self, class_id: str) -> bool:
        """A blank class is valid (an unconfirmed/optional field); otherwise must exist."""
        if class_id == "":
            return True
        return class_id in self.ids()

    def get(self, class_id: str) -> DeviceClass | None:
        for c in self.classes:
            if c.id == class_id:
                return c
        return None

    @classmethod
    def load(cls, path: Path | None = None) -> "Taxonomy":
        p = path or repo_paths()["taxonomy"]
        data = yaml.safe_load(Path(p).read_text(encoding="utf-8"))
        return cls.from_dict(data)

    @classmethod
    def from_dict(cls, data: dict) -> "Taxonomy":
        if not isinstance(data, dict) or "classes" not in data:
            raise ValueError("taxonomy.yaml must be a mapping with a 'classes' list")
        version = int(data.get("version", 1))
        classes: list[DeviceClass] = []
        seen: set[str] = set()
        for i, entry in enumerate(data["classes"]):
            if not isinstance(entry, dict) or "id" not in entry:
                raise ValueError(f"class #{i} must be a mapping with an 'id'")
            cid = str(entry["id"])
            if not _ID_RE.match(cid):
                raise ValueError(
                    f"class id {cid!r} is not a valid slug "
                    "(lowercase a-z0-9, '-' or '/' separators)"
                )
            if cid in seen:
                raise ValueError(f"duplicate class id {cid!r}")
            seen.add(cid)
            classes.append(
                DeviceClass(
                    id=cid,
                    name=str(entry.get("name", cid)),
                    notes=str(entry.get("notes", "")),
                    deprecated=bool(entry.get("deprecated", False)),
                )
            )
        for req in REQUIRED_IDS:
            if req not in seen:
                raise ValueError(f"taxonomy is missing required sentinel class {req!r}")
        return cls(version=version, classes=tuple(classes))


def c_enum_name(class_id: str) -> str:
    """'car-fob' -> 'CENSUS_CLASS_CAR_FOB'; 'water/gas-meter' -> 'CENSUS_CLASS_WATER_GAS_METER'."""
    token = re.sub(r"[^A-Za-z0-9]+", "_", class_id).strip("_").upper()
    return f"CENSUS_CLASS_{token}"
