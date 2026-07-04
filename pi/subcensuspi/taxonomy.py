"""Load the shared device_class vocabulary (System §5) — read-only, from shared/taxonomy.yaml.

The Pi consumes the taxonomy; it does not define one (Pi §10). Falls back to the System §5
list if the shared file can't be located (e.g. packaged without the repo).
"""

from __future__ import annotations

import yaml

from .paths import shared_taxonomy_path

_FALLBACK = [
    "garage", "car-fob", "tpms", "weather", "doorbell", "pir-motion",
    "energy-meter", "water/gas-meter", "remote", "thermostat",
    "smart-home", "beacon", "unknown", "other",
]


def load_classes() -> list[dict]:
    """Return [{id, name}] for each device_class."""
    path = shared_taxonomy_path()
    if path is None:
        return [{"id": c, "name": c} for c in _FALLBACK]
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    return [{"id": str(c["id"]), "name": str(c.get("name", c["id"]))} for c in data["classes"]]


def class_ids() -> list[str]:
    return [c["id"] for c in load_classes()]


def is_valid(class_id: str) -> bool:
    return class_id == "" or class_id in class_ids()
