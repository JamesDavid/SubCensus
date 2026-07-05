"""Collector runtime supervision + watchlist priority + place-scoped IQ (Pi §3, §9, §9a).

All hardware-independent: rtl_433 relaunch is driven by a mock factory, priority by fixture
watchlist rows, and IQ paths are pure derivation (the file WRITE stays TODO(hw))."""

from subcensuspi.collector import priority
from subcensuspi.collector.rtl433 import supervise_stream
from subcensuspi.collector.unknowns import iq_path_for, place_iq_dir
from subcensuspi.config import Config, DongleConfig


# --- A3: rtl_433 relaunch/backoff supervision (Pi §9) ---

def test_supervise_relaunches_dead_stream():
    calls = {"n": 0}
    slept: list[float] = []

    def factory():
        calls["n"] += 1
        if calls["n"] == 1:
            yield "a"
            yield "b"
        else:
            yield "c"

    # bound to 1 relaunch so it terminates; sleep captured, not real
    out = list(supervise_stream(factory, max_relaunches=1, backoff_s=0.5,
                                sleep=slept.append))
    assert out == ["a", "b", "c"]  # the dead stream was relaunched and drained
    assert calls["n"] == 2
    assert slept == [0.5]  # backed off once before relaunch


def test_supervise_survives_a_crashing_producer():
    calls = {"n": 0}

    def factory():
        calls["n"] += 1
        if calls["n"] == 1:
            yield "x"
            raise RuntimeError("rtl_433 crashed")
        yield "y"

    out = list(supervise_stream(factory, max_relaunches=1, sleep=lambda _s: None))
    assert out == ["x", "y"]  # a crash is just another "died" -> relaunch


def test_supervise_backoff_grows_then_stops():
    slept: list[float] = []
    list(supervise_stream(lambda: iter(()), max_relaunches=3, backoff_s=1.0,
                          backoff_max_s=3.0, sleep=slept.append))
    assert slept == [1.0, 2.0, 3.0]  # exponential, capped at backoff_max_s


# --- A5: watchlist-driven attention priority (Pi §3, opt-in) ---

def test_parse_freq_hz():
    assert priority.parse_freq_hz("433.92M") == 433_920_000
    assert priority.parse_freq_hz("915M") == 915_000_000
    assert priority.parse_freq_hz("315000000") == 315_000_000
    assert priority.parse_freq_hz(433_920_000) == 433_920_000


def test_prioritize_freqs_reorders_hot_first():
    wl = [
        {"freq_hz": "915000000", "occupancy": "0.80", "source": "recon"},
        {"freq_hz": "433920000", "occupancy": "0.10", "source": "recon"},
    ]
    out = priority.prioritize_freqs(["433.92M", "915M", "315M"], wl)
    assert out[0] == "915M"  # hottest band floated to the front
    assert out[-1] == "315M"  # no watchlist match -> keeps tail order


def test_prioritize_freqs_empty_watchlist_is_noop():
    freqs = ["433.92M", "915M", "315M"]
    assert priority.prioritize_freqs(freqs, []) == freqs  # unprioritized floor (§3)


def test_user_pin_beats_recon_occupancy():
    wl = [
        {"freq_hz": "915000000", "occupancy": "0.99", "source": "recon"},
        {"freq_hz": "315000000", "occupancy": "0.01", "source": "user-pin"},
    ]
    out = priority.prioritize_freqs(["915M", "315M"], wl)
    assert out[0] == "315M"  # a user pin is authoritative attention


def test_excluded_band_not_boosted():
    wl = [{"freq_hz": "915000000", "occupancy": "0.99", "source": "user-exclude"}]
    out = priority.prioritize_freqs(["433.92M", "915M"], wl)
    assert out == ["433.92M", "915M"]  # exclusion never floats a band up


def test_prioritize_dongles_orders_hottest_dongle_first():
    d0 = DongleConfig(serial="A", freqs=["433.92M"])
    d1 = DongleConfig(serial="B", freqs=["915M"])
    wl = [{"freq_hz": "915000000", "occupancy": "0.90", "source": "recon"}]
    out = priority.prioritize_dongles([d0, d1], wl)
    assert out[0].serial == "B"  # the dongle covering the hot band is serviced first
    # input list untouched (fresh copies returned)
    assert [d.serial for d in (d0, d1)] == ["A", "B"]


# --- A6: place-scoped captured IQ (Pi §9a) ---

def test_config_place_iq_dir_is_place_scoped():
    cfg = Config(places_dir="/var/lib/subcensuspi/places", place="garage")
    assert cfg.place_iq_dir().replace("\\", "/").endswith("places/garage/iq")
    assert cfg.place_iq_dir("attic").replace("\\", "/").endswith("places/attic/iq")


def test_iq_path_for_derives_place_scoped_cu8():
    d = place_iq_dir("/places", "home")
    assert d.name == "iq" and d.parent.name == "home"
    p = iq_path_for("/places", "home", 433_920_000, ts="2026-07-04T12:00:30")
    assert p.parent == d
    assert p.name.startswith("unk_433920000") and p.suffix == ".cu8"
