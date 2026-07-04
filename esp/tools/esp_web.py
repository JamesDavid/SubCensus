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

    def inject_sub(self, sub_text: str) -> dict:  # pragma: no cover - needs a node
        """Fixture-inject a .sub through /api/debug/inject (Esp §3.4) — drives the full
        processing path with no live RF."""
        import httpx

        r = httpx.post(f"{self.base_url}/api/debug/inject", content=sub_text.encode())
        return r.json()
