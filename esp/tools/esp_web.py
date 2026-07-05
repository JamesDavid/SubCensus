"""SubCensusEsp web-UI driver (Debug §3) — headless HTTP/WebSocket client for the node.

The ESP is headless: its UI IS a web interface, so it's driven over HTTP/WS. This module has:
  - pure parsers/validators for the served contract (status JSON, captures CSV against the
    SHARED census_log schema, WS capture messages) — unit-tested off-device.
  - a NodeClient for a running node (real hardware / OTA), plus fixture-inject over the
    /api/debug/inject endpoint (Esp §3.4) so the decode->classify->log->WS path can be driven
    without live RF once a node is flashed. Live connectivity is the only hardware need.

Cross-check: captures CSV is validated against the SAME shared schema the Zero/Pi use, so the
ESP is proven to serve tool-agnostic artifacts (System §9).
"""

from __future__ import annotations

import csv
import io
import json

# validate against the shared contract (subcensus_tools is installed from Session 1)
from subcensus_tools.schema import load_all_schemas, validate_csv  # noqa: E402
from subcensus_tools.taxonomy import Taxonomy  # noqa: E402

STATUS_KEYS = {"node", "version", "place", "mode", "wifi", "cc1101", "tx_enabled"}


def parse_status(text: str) -> dict:
    obj = json.loads(text)
    missing = STATUS_KEYS - set(obj)
    if missing:
        raise ValueError(f"/api/status missing keys: {sorted(missing)}")
    if obj["node"] != "subcensusesp":
        raise ValueError(f"unexpected node id {obj['node']!r}")
    return obj


def validate_captures_csv(text: str, tmp_path) -> list[str]:
    """Validate a served census_log.csv against the SHARED census_log schema. Returns errors."""
    p = tmp_path / "census_log.csv"
    p.write_text(text, encoding="utf-8", newline="")
    return validate_csv(load_all_schemas()["census_log"], p, Taxonomy.load())


def parse_captures_csv(text: str) -> list[dict]:
    return list(csv.DictReader(io.StringIO(text)))


SETTINGS_KEYS = {
    "place_id", "mode", "freq_preset", "capture_preset", "use_watchlist", "rssi_auto",
    "rssi_threshold", "dwell_ms", "capture_max_ms", "survey_minutes", "auto_classify",
    "match_db", "tx_enabled", "mqtt_enabled",
}


def parse_settings(text: str) -> dict:
    obj = json.loads(text)
    missing = SETTINGS_KEYS - set(obj)
    if missing:
        raise ValueError(f"/api/settings missing keys: {sorted(missing)}")
    return obj


def parse_places(text: str) -> dict:
    obj = json.loads(text)
    if "active" not in obj or "places" not in obj:
        raise ValueError("/api/places must have active + places")
    for p in obj["places"]:
        if not {"id", "name", "active"} <= set(p):
            raise ValueError(f"place entry missing keys: {p}")
    return obj


def parse_candidates(text: str) -> list[dict]:
    obj = json.loads(text)
    cands = obj.get("candidates", [])
    for c in cands:
        if not {"name", "class", "confidence", "source"} <= set(c):
            raise ValueError(f"candidate missing keys: {c}")
    return cands


def parse_taxonomy(text: str) -> list[dict]:
    """The label taxonomy the Review dropdown consumes (System §5). Each class id must be a
    real taxonomy id (validated against the SHARED taxonomy so the ESP can't drift)."""
    obj = json.loads(text)
    classes = obj.get("classes", [])
    if not classes:
        raise ValueError("/api/taxonomy has no classes")
    tax = Taxonomy.load()
    for c in classes:
        if not {"id", "name"} <= set(c):
            raise ValueError(f"taxonomy class missing keys: {c}")
        if not tax.is_valid(c["id"]):
            raise ValueError(f"taxonomy class id not in shared taxonomy: {c['id']!r}")
    return classes


FIELDMAP_KEYS = {"signature", "nbits", "n_bytes", "fields", "checksum", "confidence", "reasoning"}
FIELD_KEYS = {"name", "start_bit", "length", "class", "semantics"}
FIELD_CLASSES = {"static", "slow", "counter", "checksum", "data"}


def parse_fieldmap(text: str) -> dict:
    """A field-map discovery proposal (Esp §5, System §7b) — the passive differential overlay +
    named checksum the node serves for segment labeling. Fields round-trip to shared/core ScField.
    PROPOSAL only; never auto-committed."""
    obj = json.loads(text)
    missing = FIELDMAP_KEYS - set(obj)
    if missing:
        raise ValueError(f"/api/fieldmap missing keys: {sorted(missing)}")
    for f in obj["fields"]:
        if not FIELD_KEYS <= set(f):
            raise ValueError(f"field segment missing keys: {f}")
        if f["class"] not in FIELD_CLASSES:
            raise ValueError(f"unknown field class {f['class']!r}")
    ck = obj["checksum"]
    if ck is not None and "kind" not in ck:
        raise ValueError("checksum present but unnamed")
    return obj


def parse_ws_capture(msg: str) -> dict:
    """A live-feed WebSocket capture message (Esp §5)."""
    obj = json.loads(msg)
    for k in ("ts", "freq_hz", "rssi", "preset", "source"):
        if k not in obj:
            raise ValueError(f"ws capture message missing {k!r}")
    return obj


class NodeClient:
    """Drives a running node over HTTP/WS. TODO(hw): needs a flashed ESP32 (USB/OTA)."""

    def __init__(self, base_url: str):
        self.base_url = base_url.rstrip("/")

    def status(self) -> dict:  # pragma: no cover - needs a node
        import httpx

        return parse_status(httpx.get(f"{self.base_url}/api/status").text)

    def captures(self) -> list[dict]:  # pragma: no cover - needs a node
        import httpx

        return parse_captures_csv(httpx.get(f"{self.base_url}/api/captures").text)

    def fieldmap(self, frames_hex: str, signature: str = "unknown") -> dict:  # pragma: no cover
        """Post an aligned hex-frame corpus to /api/fieldmap and parse the proposal (Esp §5)."""
        import httpx

        r = httpx.post(
            f"{self.base_url}/api/fieldmap",
            data={"frames": frames_hex, "signature": signature},
        )
        return parse_fieldmap(r.text)

    def inject_sub(self, sub_text: str) -> dict:  # pragma: no cover - needs a node
        """Fixture-inject a .sub through /api/debug/inject (Esp §3.4) — drives the full
        processing path with no live RF."""
        import httpx

        r = httpx.post(f"{self.base_url}/api/debug/inject", content=sub_text.encode())
        return r.json()
