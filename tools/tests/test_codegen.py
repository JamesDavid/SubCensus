"""Tests for codegen (System §10) — generated headers reflect the sources and don't drift."""

from subcensus_tools import codegen
from subcensus_tools.schema import load_all_schemas
from subcensus_tools.taxonomy import Taxonomy


def test_taxonomy_header_contains_enum_and_accessors():
    h = codegen.gen_taxonomy_header(Taxonomy.load())
    assert "CENSUS_CLASS_GARAGE = 0," in h
    assert "CENSUS_CLASS_WATER_GAS_METER" in h
    assert "CENSUS_CLASS_COUNT = 14," in h
    assert "case CENSUS_CLASS_WEATHER:" in h
    assert 'return "weather";' in h
    assert "census_class_from_id" in h


def test_schema_header_contains_headers_and_indices():
    h = codegen.gen_schema_header(load_all_schemas())
    # long headers wrap onto a continuation line; the string content is still present
    assert '"ts_iso,freq_hz,rssi_dbm' in h
    assert "CENSUS_LOG_NCOLS 14" in h
    assert "CENSUS_LOG_COL_TS_ISO = 0," in h
    # short headers stay on one line
    assert 'OCCUPANCY_HEADER "freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen"' in h


def test_generated_files_up_to_date():
    """The committed generated files must match the current sources by CONTENT (System §10).

    Content-based so it stays green against clang-formatted files without this test
    clobbering their formatting. If this fails: run `python -m subcensus_tools.codegen`
    then `ufbt format`, and commit.
    """
    assert codegen.check() == []


def test_content_key_ignores_formatting():
    a = '#define X_HEADER "a,b,c"\n#define X_NCOLS 3\n'
    b = '#define X_HEADER \\\n    "a,b,c"\n#define X_NCOLS  3\n'  # clang-formatted variant
    assert codegen._content_key(a) == codegen._content_key(b)
