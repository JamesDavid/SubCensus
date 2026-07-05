"""M6: occupancy heatmap pass + shared-schema artifacts + Bands API (Pi §3, §9a).

Emitted occupancy.csv/watchlist.csv are validated against the SAME shared schema the Zero
uses (via subcensus_tools) — proving the artifacts are tool-agnostic (§9a)."""

import pytest
from fastapi.testclient import TestClient

from subcensuspi import occupancy_pass as op
from subcensuspi.web.app import create_app

# cross-tool: validate against the shared schema definitions
from subcensus_tools.schema import load_all_schemas, validate_csv
from subcensus_tools.taxonomy import Taxonomy


@pytest.fixture
def sweep(fixtures_dir):
    return fixtures_dir / "rtl_power" / "home_sweep.csv"


def test_pass_finds_hot_bins(sweep):
    bins = op.run_occupancy_pass(op.parse_rtl_power_csv(sweep))
    by_freq = {b.freq_hz: b for b in bins}
    # band A ~433.97 hot every pass -> occupancy ~1.0
    hot_a = by_freq[433970000]
    assert hot_a.occupancy == 1.0
    assert hot_a.peak_rssi == -55.0
    assert hot_a.noise_floor == -97.0  # band A quiet-bin floor
    # band B ~315.05 hot on 2 of 4 passes -> ~0.5
    hot_b = by_freq[315050000]
    assert hot_b.occupancy == 0.5
    # a quiet bin reads ~0
    assert by_freq[433470000].occupancy == 0.0


def test_emitted_csvs_match_shared_schema(sweep, tmp_path):
    place = tmp_path / "home"
    op.run_pass_to_place(sweep, place)
    tax = Taxonomy.load()
    schemas = load_all_schemas()
    assert validate_csv(schemas["occupancy"], place / "occupancy.csv", tax) == []
    assert validate_csv(schemas["watchlist"], place / "watchlist.csv", tax) == []


def test_accumulate_merges(sweep, tmp_path):
    place = tmp_path / "home"
    op.run_pass_to_place(sweep, place)  # pass 1
    first = {b.freq_hz: b for b in op.read_occupancy_csv(place / "occupancy.csv")}
    op.run_pass_to_place(sweep, place)  # pass 2 accumulates
    second = {b.freq_hz: b for b in op.read_occupancy_csv(place / "occupancy.csv")}
    hot = 433970000
    # crossings summed across passes; occupancy stays 1.0 (averaged with itself)
    assert second[hot].crossings > first[hot].crossings
    assert second[hot].occupancy == 1.0


def test_reset_keeps_or_wipes_pins(sweep, tmp_path):
    place = tmp_path / "home"
    op.run_pass_to_place(sweep, place)
    op.set_pin(place, 433970000, source="user-pin")
    assert any(p.source == "user-pin" for p in op.read_watchlist_pins(place / "watchlist.csv"))
    op.reset_place(place, keep_pins=True)
    assert any(p.source == "user-pin" for p in op.read_watchlist_pins(place / "watchlist.csv"))
    assert op.read_occupancy_csv(place / "occupancy.csv") == []
    op.reset_place(place, keep_pins=False)
    assert op.read_watchlist_pins(place / "watchlist.csv") == []


def test_bands_api(sweep, tmp_path):
    places_dir = tmp_path / "places"
    op.run_pass_to_place(sweep, places_dir / "home")
    client = TestClient(create_app(str(tmp_path / "census.db"), place="home", places_dir=str(places_dir)))

    occ = client.get("/api/occupancy").json()
    assert occ[0]["freq_hz"] in (433970000, 315050000)  # busiest first
    assert occ[0]["occupancy"] >= occ[-1]["occupancy"]

    wl = client.get("/api/watchlist").json()
    assert any(int(r["freq_hz"]) == 433970000 for r in wl)

    # pin then reset via API
    assert client.post("/api/watchlist/pin", data={"freq_hz": 433970000, "action": "pin", "place": "home"}).status_code == 200
    assert client.post("/api/recon/reset", data={"place": "home", "keep_pins": "true"}).status_code == 200
    assert client.get("/api/occupancy").json() == []


def test_bands_render_in_dashboard(sweep, tmp_path):
    places_dir = tmp_path / "places"
    op.run_pass_to_place(sweep, places_dir / "home")
    client = TestClient(create_app(str(tmp_path / "census.db"), place="home", places_dir=str(places_dir)))
    html = client.get("/").text
    assert "Bands" in html                       # §7 occupancy/watchlist section rendered
    assert "Run (Accumulate)" in html            # recon controls present
    assert "Run (Fresh)" in html
    assert "Reset (keep pins)" in html
    assert "433.970 MHz" in html                 # a hot bin shown in the heatmap table


def test_recon_run_from_recorded_sweep(sweep, tmp_path):
    """Run control (Accumulate default / Fresh) driven from a RECORDED rtl_power sweep — the
    processing path off-device. A live sweep needs a dongle (TODO(hw))."""
    places_dir = tmp_path / "places"
    client = TestClient(create_app(str(tmp_path / "census.db"), place="home", places_dir=str(places_dir)))

    # a live sweep (no recorded csv) is the hardware boundary -> 501 TODO(hw)
    r = client.post("/api/recon/run", data={"place": "home", "mode": "accumulate"})
    assert r.status_code == 501 and "TODO(hw)" in r.json()["detail"]

    # fresh run from the recorded sweep writes occupancy/watchlist artifacts
    r = client.post("/api/recon/run", data={"place": "home", "mode": "fresh", "rtl_power_csv": str(sweep)})
    assert r.status_code == 200 and r.json()["ok"] and r.json()["bins"] > 0
    assert any(b["freq_hz"] == 433970000 for b in client.get("/api/occupancy").json())

    # accumulate merges into the existing pass
    r = client.post("/api/recon/run", data={"place": "home", "mode": "accumulate", "rtl_power_csv": str(sweep)})
    assert r.status_code == 200 and r.json()["mode"] == "accumulate"

    # bad mode rejected
    assert client.post("/api/recon/run", data={"place": "home", "mode": "nope", "rtl_power_csv": str(sweep)}).status_code == 400
