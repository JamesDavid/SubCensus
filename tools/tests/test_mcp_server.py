"""MCP server surface (System §8 optional hook): tools/list + tools/call driven directly, with
an injected call_model, so the analyze round-trip runs with no network and no real MCP client.
"""

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mcp_server  # noqa: E402

CENSUS_LOG = (
    "ts_iso,freq_hz,rssi_dbm,duration_ms,preset,fsk_suspected,protocol,key,match_name,"
    "match_class,match_conf,match_source,sub_file,label\n"
    "2026-07-04T12:00:00,433920000,-60.5,0,OOK650,0,Acurite-Tower,,,,0.00,,captures/a.sub,\n"
    "2026-07-04T12:01:00,433920000,-61.0,0,OOK650,0,Acurite-Tower,,,,0.00,,captures/b.sub,\n"
)
OCCUPANCY = (
    "freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen\n"
    "433920000,-97.0,-60.0,0.80,9,2026-07-04T12:03:00\n"
)


@pytest.fixture
def zero_place(tmp_path):
    d = tmp_path / "home_1a2b"
    d.mkdir()
    (d / "census_log.csv").write_text(CENSUS_LOG, encoding="utf-8", newline="")
    (d / "occupancy.csv").write_text(OCCUPANCY, encoding="utf-8", newline="")
    return d


def _fake_model(messages):
    assert any("RF/ISM analyst" in m["content"] for m in messages)
    return "```json\n" + json.dumps({
        "identifications": [
            {"signature": "Acurite-Tower", "candidate": "weather", "confidence": 0.9},
        ],
    }) + "\n```"


def test_initialize_and_tools_list():
    srv = mcp_server.SubCensusMCP()
    init = srv.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
    assert init["result"]["protocolVersion"] == mcp_server.PROTOCOL_VERSION
    assert init["result"]["serverInfo"]["name"] == "subcensus"

    # notification -> no response
    assert srv.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}) is None

    listed = srv.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
    names = {t["name"] for t in listed["result"]["tools"]}
    assert names == {"export_place", "analyze_place"}
    # each tool advertises a JSON schema requiring place_dir
    for t in listed["result"]["tools"]:
        assert t["inputSchema"]["required"] == ["place_dir"]


def test_tools_call_export_place(zero_place):
    srv = mcp_server.SubCensusMCP()
    resp = srv.handle({
        "jsonrpc": "2.0", "id": 3, "method": "tools/call",
        "params": {"name": "export_place", "arguments": {"place_dir": str(zero_place)}},
    })
    assert resp["result"]["isError"] is False
    bundle = json.loads(resp["result"]["content"][0]["text"])
    assert bundle["manifest"]["capture_count"] == 2
    assert bundle["manifest"]["place"] == "home_1a2b"


def test_tools_call_analyze_place_injected_model(zero_place):
    srv = mcp_server.SubCensusMCP(call_model=_fake_model)
    resp = srv.handle({
        "jsonrpc": "2.0", "id": 4, "method": "tools/call",
        "params": {"name": "analyze_place",
                   "arguments": {"place_dir": str(zero_place), "confidence_floor": 0.8}},
    })
    assert resp["result"]["isError"] is False
    payload = json.loads(resp["result"]["content"][0]["text"])
    assert "analysis" in payload and "proposed_labels" in payload
    # required analysis keys defaulted by analyze_bundle
    assert payload["analysis"]["field_maps"] == []
    assert [p["candidate"] for p in payload["proposed_labels"]] == ["weather"]


def test_analyze_without_model_is_tool_error(zero_place):
    srv = mcp_server.SubCensusMCP()  # no model configured
    resp = srv.handle({
        "jsonrpc": "2.0", "id": 5, "method": "tools/call",
        "params": {"name": "analyze_place", "arguments": {"place_dir": str(zero_place)}},
    })
    assert "error" in resp and resp["error"]["code"] == mcp_server._INTERNAL_ERROR


def test_unknown_method_and_unknown_tool(zero_place):
    srv = mcp_server.SubCensusMCP()
    bad_method = srv.handle({"jsonrpc": "2.0", "id": 6, "method": "no/such"})
    assert bad_method["error"]["code"] == mcp_server._METHOD_NOT_FOUND
    bad_tool = srv.handle({
        "jsonrpc": "2.0", "id": 7, "method": "tools/call",
        "params": {"name": "nope", "arguments": {}},
    })
    assert bad_tool["error"]["code"] == mcp_server._INVALID_PARAMS


def test_call_tool_direct(zero_place):
    # the tool layer is drivable without the JSON-RPC envelope too
    srv = mcp_server.SubCensusMCP(call_model=_fake_model)
    bundle = srv.call_tool("export_place", {"place_dir": str(zero_place)})
    assert bundle["manifest"]["capture_count"] == 2
    result = srv.call_tool("analyze_place", {"place_dir": str(zero_place)})
    assert result["proposed_labels"][0]["candidate"] == "weather"
