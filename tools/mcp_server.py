#!/usr/bin/env python3
"""mcp_server — expose SubCensus place analysis as MCP tools (System §8 optional hook).

System §8: "Optional future hook: expose analyze_place as an MCP tool so an agent can query a
place's inventory directly." This is that hook. It serves two tools over stdio JSON-RPC:

  - export_place(place_dir, name?, protocol_map?)  -> the rolled-up analysis bundle (System §8)
  - analyze_place(place_dir, name?, confidence_floor?) -> structured analysis + proposed labels

The `mcp` package is NOT a dependency, so this implements the minimal MCP stdio JSON-RPC surface
by hand: `initialize`, `tools/list`, `tools/call`. The whole thing is importable and unit-testable
with NO network and NO real MCP client — drive `SubCensusMCP.handle(...)` / `.call_tool(...)`
directly, injecting a `call_model` for the analyze round-trip (the model call is the only network
edge, and it is injectable, mirroring analyze_place.analyze_bundle).

Run as a stdio MCP server (real inference is opt-in, System §8):
  python tools/mcp_server.py --provider openai-compatible --base-url http://localhost:11434/v1 \
      --model llama3
An agent host speaks newline-delimited JSON-RPC on stdin/stdout.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Callable

# tools/ on sys.path so the sibling top-level scripts import whether run as a script or imported.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from subcensus_tools import brain
from subcensus_tools.place_bundle import build_bundle

CallModel = Callable[[list[dict]], str]
PROTOCOL_VERSION = "2024-11-05"

# JSON-RPC error codes (subset) — https://www.jsonrpc.org/specification
_METHOD_NOT_FOUND = -32601
_INVALID_PARAMS = -32602
_INTERNAL_ERROR = -32603

_TOOLS = [
    {
        "name": "export_place",
        "description": "Roll a SubCensus place folder (Zero CSV or Esp SD) into the shared "
                       "analysis bundle: manifest, occupancy digest + coverage gaps, device "
                       "roll-up (Identified vs Needs-ID) with cadence, full feature vectors for "
                       "unknowns, and reference grounding (System §8).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "place_dir": {"type": "string",
                              "description": "Path to the place folder (census_log.csv + occupancy.csv)."},
                "name": {"type": "string", "description": "Display name (default: folder name)."},
                "protocol_map": {"type": "string",
                                 "description": "Optional protocol_map.csv for reference grounding."},
            },
            "required": ["place_dir"],
        },
    },
    {
        "name": "analyze_place",
        "description": "Export the place bundle and run the RF/ISM analyst pass, returning the "
                       "structured analysis (inventory, identifications, field_maps, anomalies, "
                       "coverage_gaps, recommended_actions) plus the high-confidence proposed "
                       "labels. Never auto-applies labels (System §6/§8).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "place_dir": {"type": "string", "description": "Path to the place folder."},
                "name": {"type": "string", "description": "Display name (default: folder name)."},
                "confidence_floor": {"type": "number",
                                     "description": "Min confidence for a proposed label (default 0.8)."},
            },
            "required": ["place_dir"],
        },
    },
]


class SubCensusMCP:
    """Minimal MCP handler over the SubCensus place tools. `call_model` is injected so the
    analyze round-trip is testable with no network (System §8)."""

    def __init__(self, call_model: CallModel | None = None):
        self._call_model = call_model

    # --- tool implementations ---

    def export_place(self, place_dir: str, name: str | None = None,
                     protocol_map: str | None = None) -> dict:
        pm = brain.read_protocol_map(protocol_map) if protocol_map else []
        return build_bundle(place_dir, place_name=name, protocol_map=pm)

    def analyze_place(self, place_dir: str, name: str | None = None,
                      protocol_map: str | None = None, confidence_floor: float = 0.8) -> dict:
        if self._call_model is None:
            raise RuntimeError("analyze_place requires a call_model (provider not configured)")
        # imported lazily so export-only use needs no analyze_place import chain
        from analyze_place import analyze_bundle, proposed_labels

        bundle = self.export_place(place_dir, name=name, protocol_map=protocol_map)
        analysis = analyze_bundle(bundle, self._call_model)
        return {
            "analysis": analysis,
            "proposed_labels": proposed_labels(analysis, confidence_floor),
        }

    # --- MCP surface ---

    def list_tools(self) -> list[dict]:
        return _TOOLS

    def call_tool(self, name: str, arguments: dict | None) -> dict:
        args = dict(arguments or {})
        if name == "export_place":
            return self.export_place(**args)
        if name == "analyze_place":
            return self.analyze_place(**args)
        raise ValueError(f"unknown tool {name!r}")

    def handle(self, request: dict) -> dict | None:
        """Dispatch one JSON-RPC request; return the response, or None for a notification."""
        rid = request.get("id")
        method = request.get("method")
        params = request.get("params") or {}

        if method == "initialize":
            return _ok(rid, {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "subcensus", "version": "0.1.0"},
            })
        if method in ("notifications/initialized", "initialized"):
            return None  # notification: no response
        if method == "tools/list":
            return _ok(rid, {"tools": self.list_tools()})
        if method == "tools/call":
            name = params.get("name")
            try:
                result = self.call_tool(name, params.get("arguments"))
            except (ValueError, TypeError) as e:
                return _err(rid, _INVALID_PARAMS, str(e))
            except Exception as e:  # noqa: BLE001 - surface any tool failure as a tool error
                return _err(rid, _INTERNAL_ERROR, str(e))
            return _ok(rid, {
                "content": [{"type": "text", "text": json.dumps(result, sort_keys=True)}],
                "isError": False,
            })
        return _err(rid, _METHOD_NOT_FOUND, f"unknown method {method!r}")


def _ok(rid, result: dict) -> dict:
    return {"jsonrpc": "2.0", "id": rid, "result": result}


def _err(rid, code: int, message: str) -> dict:
    return {"jsonrpc": "2.0", "id": rid, "error": {"code": code, "message": message}}


def serve_stdio(server: SubCensusMCP, stdin=None, stdout=None) -> None:  # pragma: no cover - I/O loop
    """Newline-delimited JSON-RPC over stdio. Each stdin line is one request; each response is
    one line on stdout. No framing headers (keeps it dependency-free and easy to drive)."""
    stdin = stdin or sys.stdin
    stdout = stdout or sys.stdout
    for line in stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
        except json.JSONDecodeError:
            continue
        response = server.handle(request)
        if response is not None:
            stdout.write(json.dumps(response) + "\n")
            stdout.flush()


def main(argv: list[str] | None = None) -> int:  # pragma: no cover - needs a provider/stdio
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--provider", choices=["anthropic", "openai-compatible", "local"], default="local")
    ap.add_argument("--base-url", default="http://localhost:11434/v1")
    ap.add_argument("--model", default="llama3")
    ap.add_argument("--api-key", default=os.environ.get("SUBCENSUS_API_KEY"))
    args = ap.parse_args(argv)

    from analyze_place import make_call_model
    call_model = make_call_model(args.provider, args.base_url, args.model, args.api_key)
    serve_stdio(SubCensusMCP(call_model=call_model))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
