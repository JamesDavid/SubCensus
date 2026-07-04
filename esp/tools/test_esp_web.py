"""Tests for the ESP web-UI driver's pure parsers/validators (Debug §3). No node needed.

Run: python -m pytest esp/tools/
The served payloads here mirror exactly what src/main.cpp produces, so the contract is
validated off-device; live driving is covered by NodeClient (TODO(hw))."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import esp_web  # noqa: E402


# what src/main.cpp status_json() serves
STATUS = (
    '{"node":"subcensusesp","version":"0.1","place":"home_1a2b","mode":2,'
    '"camp_running":false,"wifi":{"connected":false,"ip":"0.0.0.0"},'
    '"cc1101":{"present":false,"version":0},"tx_enabled":false}'
)

# what /api/captures serves (header from the shared schema + rows main.cpp appends)
CAPTURES = (
    "ts_iso,freq_hz,rssi_dbm,duration_ms,preset,fsk_suspected,protocol,key,match_name,"
    "match_class,match_conf,match_source,sub_file,label\n"
    "2026-07-04T12:00:00,433920000,-60.5,0,OOK650,0,,,,,0.00,,captures/x.sub,\n"
)

# a WebSocket live-feed message main.cpp broadcasts
WS_MSG = (
    '{"ts":"2026-07-04T12:00:00","freq_hz":433920000,"rssi":-60.5,"preset":"OOK650",'
    '"n_symbols":45,"sub":"captures/x.sub","source":"inject"}'
)


def test_parse_status():
    s = esp_web.parse_status(STATUS)
    assert s["node"] == "subcensusesp"
    assert s["cc1101"]["present"] is False
    assert s["place"] == "home_1a2b"


def test_parse_status_rejects_wrong_node():
    with pytest.raises(ValueError):
        esp_web.parse_status('{"node":"nope","version":"0.1","place":"p","mode":0,'
                             '"wifi":{},"cc1101":{},"tx_enabled":false}')


def test_captures_csv_matches_shared_schema(tmp_path):
    # the ESP serves census_log conforming to the SAME shared schema as the Zero/Pi
    assert esp_web.validate_captures_csv(CAPTURES, tmp_path) == []
    rows = esp_web.parse_captures_csv(CAPTURES)
    assert len(rows) == 1
    assert rows[0]["freq_hz"] == "433920000"
    assert rows[0]["preset"] == "OOK650"


def test_parse_ws_capture():
    m = esp_web.parse_ws_capture(WS_MSG)
    assert m["freq_hz"] == 433920000
    assert m["source"] == "inject"
    assert m["preset"] == "OOK650"
    with pytest.raises(ValueError):
        esp_web.parse_ws_capture('{"ts":"x"}')
