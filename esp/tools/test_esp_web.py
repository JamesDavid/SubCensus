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


SETTINGS = (
    '{"place_id":"home","mode":2,"freq_preset":0,"capture_preset":0,"use_watchlist":true,'
    '"rssi_auto":true,"rssi_threshold":-80,"dwell_ms":80,"capture_max_ms":1500,'
    '"survey_minutes":15,"auto_classify":true,"match_db":true,"tx_enabled":false,'
    '"mqtt_enabled":false}'
)
PLACES = '{"active":"home","places":[{"id":"home","name":"Home","active":true},' \
         '{"id":"garage_1a2b","name":"Garage","active":false}]}'
CANDIDATES = '{"candidates":[{"name":"PT2262 remote","class":"remote","confidence":0.912,"source":"fingerprint"}]}'


def test_parse_settings():
    s = esp_web.parse_settings(SETTINGS)
    assert s["mode"] == 2 and s["tx_enabled"] is False and s["place_id"] == "home"


def test_parse_places():
    p = esp_web.parse_places(PLACES)
    assert p["active"] == "home"
    assert {x["id"] for x in p["places"]} == {"home", "garage_1a2b"}
    assert any(x["active"] for x in p["places"])


def test_parse_candidates():
    c = esp_web.parse_candidates(CANDIDATES)
    assert c[0]["class"] == "remote" and c[0]["source"] == "fingerprint"


# what /api/fieldmap serves (mirrors esp_fieldmap_to_json in src/esp_fieldmap.c — the passive
# differential overlay + named checksum; fields round-trip to shared/core ScField)
FIELDMAP = (
    '{"signature":"acurite:0x1234","nbits":32,"n_bytes":4,"modulation":0,"user_confirmed":false,'
    '"fields":[{"name":"byte0","start_bit":0,"length":8,"class":"static","semantics":null},'
    '{"name":"byte1","start_bit":8,"length":8,"class":"counter","semantics":null},'
    '{"name":"byte2","start_bit":16,"length":8,"class":"slow","semantics":"tracks temperature"},'
    '{"name":"byte3","start_bit":24,"length":8,"class":"checksum","semantics":null}],'
    '"checksum":{"kind":"xor","poly":0,"init":0,"gen":0,"key":0,"over_bytes":3},'
    '"confidence":0.700,"reasoning":"4 byte-segments; PROPOSAL - passive (no TX)."}'
)


def test_parse_fieldmap():
    fm = esp_web.parse_fieldmap(FIELDMAP)
    assert fm["signature"] == "acurite:0x1234"
    assert fm["user_confirmed"] is False  # a proposal, never auto-committed (System §7b)
    assert [f["class"] for f in fm["fields"]] == ["static", "counter", "slow", "checksum"]
    assert fm["checksum"]["kind"] == "xor"
    assert fm["fields"][2]["semantics"] == "tracks temperature"


def test_parse_fieldmap_rejects_bad_class():
    bad = FIELDMAP.replace('"class":"static"', '"class":"bogus"')
    with pytest.raises(ValueError):
        esp_web.parse_fieldmap(bad)


def test_parse_fieldmap_no_checksum():
    fm = esp_web.parse_fieldmap(FIELDMAP.replace(
        '"checksum":{"kind":"xor","poly":0,"init":0,"gen":0,"key":0,"over_bytes":3}',
        '"checksum":null'))
    assert fm["checksum"] is None


def test_parse_ws_capture():
    m = esp_web.parse_ws_capture(WS_MSG)
    assert m["freq_hz"] == 433920000
    assert m["source"] == "inject"
    assert m["preset"] == "OOK650"
    with pytest.raises(ValueError):
        esp_web.parse_ws_capture('{"ts":"x"}')
