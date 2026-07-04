"""Tests for the taxonomy loader (System §5)."""

import pytest

from subcensus_tools.taxonomy import (
    REQUIRED_IDS,
    DeviceClass,
    Taxonomy,
    c_enum_name,
)


def test_loads_real_taxonomy():
    tax = Taxonomy.load()
    ids = tax.ids()
    # Exact vocabulary from System §5.
    assert ids == [
        "garage", "car-fob", "tpms", "weather", "doorbell", "pir-motion",
        "energy-meter", "water/gas-meter", "remote", "thermostat",
        "smart-home", "beacon", "unknown", "other",
    ]


def test_required_sentinels_present():
    tax = Taxonomy.load()
    for req in REQUIRED_IDS:
        assert req in tax.ids()


def test_is_valid_and_blank():
    tax = Taxonomy.load()
    assert tax.is_valid("weather")
    assert tax.is_valid("")          # blank = unset/optional, allowed
    assert not tax.is_valid("nope")


@pytest.mark.parametrize(
    "cid,expected",
    [
        ("garage", "CENSUS_CLASS_GARAGE"),
        ("car-fob", "CENSUS_CLASS_CAR_FOB"),
        ("water/gas-meter", "CENSUS_CLASS_WATER_GAS_METER"),
        ("pir-motion", "CENSUS_CLASS_PIR_MOTION"),
    ],
)
def test_c_enum_name(cid, expected):
    assert c_enum_name(cid) == expected


def test_rejects_duplicate_ids():
    with pytest.raises(ValueError):
        Taxonomy.from_dict({"version": 1, "classes": [
            {"id": "unknown"}, {"id": "other"}, {"id": "unknown"},
        ]})


def test_rejects_missing_sentinel():
    with pytest.raises(ValueError):
        Taxonomy.from_dict({"version": 1, "classes": [{"id": "garage"}, {"id": "other"}]})


def test_rejects_bad_slug():
    with pytest.raises(ValueError):
        Taxonomy.from_dict({"version": 1, "classes": [
            {"id": "Bad Id"}, {"id": "unknown"}, {"id": "other"},
        ]})


def test_deprecated_kept_but_filtered_from_active():
    tax = Taxonomy.from_dict({"version": 1, "classes": [
        {"id": "garage"}, {"id": "old-thing", "deprecated": True},
        {"id": "unknown"}, {"id": "other"},
    ]})
    assert "old-thing" in tax.ids()
    assert DeviceClass("old-thing", "old-thing", deprecated=True) not in tax.active()
