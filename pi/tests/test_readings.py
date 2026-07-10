"""Human-readable decoded readings (Pi §7): turn a reception's stored rtl_433 JSON into the short
line shown in the Devices "Latest reading" column and the Live feed. Presentation only — plumbing
keys (identity/RF/timing) are dropped, measurement keys are labeled + unit-suffixed, battery is
read as OK/LOW, and unknown fields still show via a generic fallback."""

from subcensuspi.readings import humanize_reading, raw_bits, reading_fields


def test_raw_bits_extracted_when_present():
    # rtl_433 raw payload lives under data/code/rows/... — keep it independent of the decode
    assert raw_bits('{"model":"RawSensor","data":"a50014b1","temperature_C":20}') == "a50014b1"
    assert raw_bits('{"model":"X","rows":["0101","1100"]}') == "0101 1100"


def test_raw_bits_empty_when_only_decoded_values():
    # a decoded sensor with no raw field retained -> nothing to re-interpret (the gap -M bits fixes)
    assert raw_bits('{"model":"Acurite-Tower","temperature_C":21,"humidity":45}') == ""
    assert raw_bits(None) == "" and raw_bits("not json") == ""


def test_temp_humidity():
    raw = '{"time":"t","model":"Acurite-Tower","id":1234,"channel":"A","freq":433.92,' \
          '"rssi":-60.5,"snr":12.0,"noise":-72.5,"temperature_C":21.3,"humidity":45}'
    assert humanize_reading(raw) == "temp 21.3°C · humidity 45%"


def test_plumbing_only_is_empty():
    # identity/RF only (no decoded payload) -> nothing human to show
    assert humanize_reading('{"model":"X","id":1,"channel":"A","freq":315.0,"snr":10}') == ""


def test_tpms_pressure_temp():
    raw = '{"model":"Schrader","type":"TPMS","id":"9ABCDE","freq":315.0,"snr":10.0,' \
          '"pressure_kPa":230.0,"temperature_C":22.0}'
    # float that is integer-valued renders without trailing .0
    assert humanize_reading(raw) == "pressure 230 kPa · temp 22°C"


def test_battery_flag_words():
    assert humanize_reading('{"model":"m","battery_ok":1,"temperature_C":19}') == \
        "battery OK · temp 19°C"
    assert humanize_reading('{"model":"m","battery_ok":0}') == "battery LOW"


def test_power_energy():
    assert humanize_reading('{"model":"Efergy-e2CT","id":0,"power_W":812.5}') == "power 812.5 W"


def test_unknown_field_generic_fallback():
    # a field we don't have a unit for still shows (snake_case -> words)
    assert reading_fields('{"model":"m","flow_rate":3}') == [("flow rate", "3")]


def test_bad_json_is_empty():
    assert humanize_reading("not json") == "" and humanize_reading("") == "" \
        and humanize_reading(None) == ""
