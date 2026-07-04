"""Tests for codegen (System §10) — generated headers reflect the sources and don't drift."""

from subcensus_tools import codegen
from subcensus_tools.schema import load_all_schemas
from subcensus_tools.taxonomy import Taxonomy


def test_taxonomy_header_contains_enum_and_accessors():
    h = codegen.gen_taxonomy_header(Taxonomy.load())
    assert "CENSUS_CLASS_GARAGE = 0," in h
    assert "CENSUS_CLASS_WATER_GAS_METER" in h
    assert "CENSUS_CLASS_COUNT = 14," in h
    assert 'case CENSUS_CLASS_WEATHER: return "weather";' in h
    assert "census_class_from_id" in h


def test_schema_header_contains_headers_and_indices():
    h = codegen.gen_schema_header(load_all_schemas())
    assert 'CENSUS_LOG_HEADER "ts_iso,freq_hz,rssi_dbm' in h
    assert "CENSUS_LOG_NCOLS 14" in h
    assert "CENSUS_LOG_COL_TS_ISO = 0," in h
    assert 'OCCUPANCY_HEADER "freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen"' in h


def test_generated_files_up_to_date():
    """After write(), --check must be clean. This is the no-drift guarantee (System §10)."""
    codegen.write()
    assert codegen.check() == []
