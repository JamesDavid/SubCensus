"""Tests for the schema loader + CSV validator (System §7, §9)."""

import textwrap

import pytest

from subcensus_tools.schema import Schema, load_all_schemas, validate_csv, validate_row
from subcensus_tools.taxonomy import Taxonomy


@pytest.fixture(scope="module")
def tax():
    return Taxonomy.load()


@pytest.fixture(scope="module")
def schemas():
    return load_all_schemas()


def test_all_schemas_load(schemas):
    for name in ("fingerprints", "protocol_map", "occupancy", "watchlist",
                 "catalog_record", "census_log"):
        assert name in schemas


def test_occupancy_header_matches_system_9(schemas):
    assert schemas["occupancy"].header() == [
        "freq_hz", "noise_floor", "peak_rssi", "occupancy", "crossings", "last_seen",
    ]


def test_watchlist_header_matches_system_9(schemas):
    assert schemas["watchlist"].header() == [
        "freq_hz", "modulation", "threshold_dbm", "occupancy", "source",
    ]


def test_census_log_header_matches_zero_5_4(schemas):
    assert schemas["census_log"].header() == [
        "ts_iso", "freq_hz", "rssi_dbm", "duration_ms", "preset", "fsk_suspected",
        "protocol", "key", "match_name", "match_class", "match_conf", "match_source",
        "sub_file", "label",
    ]


def test_fingerprints_expands_sym_dur(schemas):
    h = schemas["fingerprints"].header()
    assert "sym_dur_us_1" in h and "sym_dur_us_2" in h and "sym_dur_us_3" in h


def test_valid_occupancy_csv(tmp_path, schemas, tax):
    p = tmp_path / "occupancy.csv"
    p.write_text(textwrap.dedent("""\
        freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen
        433920000,-95.0,-60.5,0.12,7,2026-07-04T12:00:00
        """), encoding="utf-8")
    assert validate_csv(schemas["occupancy"], p, tax) == []


def test_header_mismatch_detected(tmp_path, schemas, tax):
    p = tmp_path / "bad.csv"
    p.write_text("freq_hz,peak_rssi\n433920000,-60\n", encoding="utf-8")
    errs = validate_csv(schemas["occupancy"], p, tax)
    assert errs and "header mismatch" in errs[0]


def test_bad_enum_and_class(schemas, tax):
    row = {
        "freq_hz": "433920000", "modulation": "PDQ", "threshold_dbm": "-80",
        "occupancy": "0.5", "source": "recon",
    }
    errs = validate_row(schemas["watchlist"], row, tax)
    assert any("modulation" in e for e in errs)


def test_device_class_validated_against_taxonomy(schemas, tax):
    cols = schemas["census_log"].header()
    row = {c: "" for c in cols}
    row.update({
        "ts_iso": "2026-07-04T12:00:00", "freq_hz": "433920000", "rssi_dbm": "-60",
        "duration_ms": "1500", "preset": "AM650", "fsk_suspected": "0",
        "label": "not-a-class",
    })
    errs = validate_row(schemas["census_log"], row, tax)
    assert any("label" in e for e in errs)
    row["label"] = "weather"
    assert not any("label" in e for e in validate_row(schemas["census_log"], row, tax))


def test_bool_must_be_0_or_1(schemas, tax):
    cols = schemas["census_log"].header()
    row = {c: "" for c in cols}
    row.update({
        "ts_iso": "2026-07-04T12:00:00", "freq_hz": "1", "rssi_dbm": "-1",
        "duration_ms": "1", "preset": "x", "fsk_suspected": "true",
    })
    errs = validate_row(schemas["census_log"], row, tax)
    assert any("fsk_suspected" in e for e in errs)


def test_empty_optional_ok_required_not(schemas, tax):
    s = Schema.from_dict({"name": "t", "columns": [
        {"name": "a", "type": "int", "required": True},
        {"name": "b", "type": "int", "required": False},
    ]})
    assert validate_row(s, {"a": "5", "b": ""}, tax) == []
    assert validate_row(s, {"a": "", "b": ""}, tax) == ["a: required but empty"]
