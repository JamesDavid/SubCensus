"""Decode plausibility / confidence gate (System §6 "Confidence & honesty"). Score each decode on
physical sanity + corroboration; the -40 °C Opus null, the 96 A Efergy, and the heard-once phantom
all score low with a stated reason, while a repeated sane reading scores high. No RF, no deletion."""

from subcensuspi.plausibility import CONFIDENCE_FLOOR, assess


def test_sane_repeated_reading_is_confident():
    a = assess('{"model":"Acurite-Tower","temperature_C":21,"humidity":45}',
               model="Acurite-Tower", count=50)
    assert a.plausible and a.confidence >= CONFIDENCE_FLOOR
    assert a.reasons == []  # nothing to flag


def test_opus_minus40_sentinel_is_implausible():
    a = assess('{"model":"Opus-XT300","moisture":0,"temperature_C":-40}',
               model="Opus-XT300", count=4)
    assert not a.plausible and a.confidence < CONFIDENCE_FLOOR
    assert any("null value" in r for r in a.reasons)


def test_all_zero_frame_is_implausible():
    a = assess('{"model":"Foo","temperature_C":0,"humidity":0}', model="Foo", count=5)
    assert not a.plausible and any("all sensor values are zero" in r for r in a.reasons)


def test_out_of_range_current_is_implausible():
    a = assess('{"model":"Efergy-e2CT","current":96.1}', model="Efergy-e2CT", count=1)
    assert not a.plausible
    assert any("outside the plausible range" in r for r in a.reasons)


def test_seen_once_penalized_even_if_values_sane():
    once = assess('{"model":"Prologue-TH","temperature_C":19,"humidity":52}',
                  model="Prologue-TH", count=1)
    many = assess('{"model":"Prologue-TH","temperature_C":19,"humidity":52}',
                  model="Prologue-TH", count=20)
    assert once.confidence < many.confidence
    assert any("heard only once" in r for r in once.reasons)


def test_noisy_decoder_starts_lower():
    efergy = assess('{"model":"Efergy-e2CT","current":3.0}', model="Efergy-e2CT", count=2)
    normal = assess('{"model":"Acurite-Tower","temperature_C":21}', model="Acurite-Tower", count=2)
    assert efergy.confidence < normal.confidence
    assert any("false-matches on noise" in r for r in efergy.reasons)


def test_empty_or_bad_json_not_crashing():
    assert assess(None, count=1).confidence >= 0.0
    assert assess("not json", count=1).confidence >= 0.0
