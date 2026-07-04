"""Occupancy heatmap pass (Pi §3, §9a) — SubCensusPi's analog of the Zero's Recon Stage A.

Different engine (wideband rtl_power/soapy_power power sweep vs the Zero's stepped RSSI), but
the SAME artifacts: per-place `occupancy.csv` + `watchlist.csv` in the shared schema (§9a), so
they are tool-agnostic and interchangeable. Cumulative per place (System §9): a re-run
accumulates into occupancy.csv; watchlist regeneration preserves user pins/exclusions; Reset
wipes (keep-or-wipe pins).

rtl_power isn't required for the *processing*: the pass is driven from a recorded rtl_power CSV
fixture (Debug §4). Only the live sweep needs a dongle (TODO(hw)).
"""

from __future__ import annotations

import csv
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

from .dsp.occupancy import OccupancyBin, watchlist_from_occupancy

# Shared-schema headers (System §9). Kept in sync with shared/schema/*.schema.yaml.
OCCUPANCY_HEADER = ["freq_hz", "noise_floor", "peak_rssi", "occupancy", "crossings", "last_seen"]
WATCHLIST_HEADER = ["freq_hz", "modulation", "threshold_dbm", "occupancy", "source"]

DEFAULT_MARGIN_DB = 12.0
DEFAULT_OCC_CUTOFF = 0.10


@dataclass
class OccBin:
    freq_hz: int
    noise_floor: float
    peak_rssi: float
    occupancy: float
    crossings: int
    last_seen: str  # ISO (occupancy.csv stores ts)


@dataclass
class Pin:
    freq_hz: int
    modulation: str
    threshold_dbm: float
    occupancy: float
    source: str  # user-pin | user-exclude


# --- rtl_power parsing ---

def parse_rtl_power_csv(path: str | Path):
    """Yield (freq_hz, dbm, ts_iso, band_low) samples from an rtl_power CSV (bins expanded).
    band_low identifies the sweep band, so the noise floor can be estimated per band (§3.3)."""
    with Path(path).open("r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 7:
                continue
            date, tm = parts[0], parts[1]
            low, high, step = int(parts[2]), int(parts[3]), int(parts[4])
            ts = f"{date}T{tm}"
            dbms = parts[6:]
            nbins = round((high - low) / step)
            for i in range(min(nbins, len(dbms))):
                try:
                    dbm = float(dbms[i])
                except ValueError:
                    continue
                center = int(low + step * i + step // 2)
                yield center, dbm, ts, low


# --- occupancy computation ---

def run_occupancy_pass(samples, margin_db: float = DEFAULT_MARGIN_DB) -> list[OccBin]:
    """Per-BAND adaptive-threshold occupancy (System §3.3 Stage A). The noise floor is
    estimated from the quiet bins of each sweep band (so an always-hot bin still reads as
    occupied); threshold = band noise floor + margin; occupancy = fraction above."""
    groups: dict[int, list[tuple[float, str]]] = defaultdict(list)
    bin_band: dict[int, int] = {}
    band_dbms: dict[int, list[float]] = defaultdict(list)
    for freq_hz, dbm, ts, band in samples:
        groups[freq_hz].append((dbm, ts))
        bin_band[freq_hz] = band
        band_dbms[band].append(dbm)

    band_floor = {band: min(dbms) for band, dbms in band_dbms.items()}

    bins: list[OccBin] = []
    for freq_hz, samps in groups.items():
        floor = band_floor[bin_band[freq_hz]]
        peak = max(d for d, _ in samps)
        threshold = floor + margin_db
        above = 0
        crossings = 0
        prev = False
        last_seen = samps[-1][1]  # default: last scan time of this bin
        for dbm, ts in samps:
            a = dbm >= threshold
            if a:
                above += 1
                last_seen = ts
                if not prev:
                    crossings += 1
            prev = a
        bins.append(OccBin(freq_hz, floor, peak, above / len(samps), crossings, last_seen))
    bins.sort(key=lambda b: b.freq_hz)
    return bins


# --- CSV IO (shared schema) ---

def write_occupancy_csv(bins: list[OccBin], path: str | Path) -> None:
    with Path(path).open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(OCCUPANCY_HEADER)
        for b in bins:
            w.writerow([b.freq_hz, b.noise_floor, b.peak_rssi,
                        round(b.occupancy, 4), b.crossings, b.last_seen])


def read_occupancy_csv(path: str | Path) -> list[OccBin]:
    p = Path(path)
    if not p.exists():
        return []
    out: list[OccBin] = []
    with p.open("r", newline="", encoding="utf-8") as fh:
        r = csv.DictReader(fh)
        for row in r:
            out.append(OccBin(
                int(row["freq_hz"]), float(row["noise_floor"]), float(row["peak_rssi"]),
                float(row["occupancy"]), int(row["crossings"]), row["last_seen"],
            ))
    return out


def read_watchlist_pins(path: str | Path) -> list[Pin]:
    """User pins/exclusions to preserve across regeneration (source=user-*)."""
    p = Path(path)
    if not p.exists():
        return []
    pins: list[Pin] = []
    with p.open("r", newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            if row.get("source", "").startswith("user-"):
                pins.append(Pin(int(row["freq_hz"]), row["modulation"],
                                float(row["threshold_dbm"]), float(row["occupancy"]),
                                row["source"]))
    return pins


def write_watchlist_csv(bins: list[OccBin], path: str | Path, *, occ_cutoff: float = DEFAULT_OCC_CUTOFF,
                        margin_db: float = DEFAULT_MARGIN_DB, pins: list[Pin] | None = None) -> None:
    """Derive watchlist from occupancy bins (recon), preserving user pins/exclusions (§9)."""
    dsp_bins = [OccupancyBin(b.freq_hz, b.noise_floor, b.peak_rssi, b.occupancy, b.crossings, 0) for b in bins]
    derived = watchlist_from_occupancy(dsp_bins, occ_cutoff, margin_db)
    pins = pins or []
    pinned_freqs = {p.freq_hz for p in pins}
    with Path(path).open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(WATCHLIST_HEADER)
        for p in pins:  # user pins/exclusions first, always preserved
            w.writerow([p.freq_hz, p.modulation, p.threshold_dbm, round(p.occupancy, 4), p.source])
        for e in derived:
            if e.freq_hz in pinned_freqs:
                continue  # a user pin/exclude overrides the recon entry
            w.writerow([e.freq_hz, e.modulation or "OOK", e.threshold_dbm, round(e.occupancy, 4), "recon"])


# --- lifecycle (System §9) ---

def accumulate(existing: list[OccBin], new: list[OccBin]) -> list[OccBin]:
    """Merge a new pass into existing bins: peak=max, crossings summed, occupancy averaged
    (equal weight), noise_floor min, last_seen=max (System §9 accumulate)."""
    by_freq = {b.freq_hz: b for b in existing}
    for nb in new:
        if nb.freq_hz in by_freq:
            b = by_freq[nb.freq_hz]
            b.peak_rssi = max(b.peak_rssi, nb.peak_rssi)
            b.noise_floor = min(b.noise_floor, nb.noise_floor)
            b.crossings += nb.crossings
            b.occupancy = (b.occupancy + nb.occupancy) / 2.0
            b.last_seen = max(b.last_seen, nb.last_seen)
        else:
            by_freq[nb.freq_hz] = nb
    return sorted(by_freq.values(), key=lambda b: b.freq_hz)


def run_pass_to_place(
    rtl_power_csv: str | Path, place_dir: str | Path, *, fresh: bool = False,
    margin_db: float = DEFAULT_MARGIN_DB, occ_cutoff: float = DEFAULT_OCC_CUTOFF,
) -> list[OccBin]:
    """Run an occupancy pass and write occupancy.csv + watchlist.csv into a place dir.
    Accumulate (default) merges into existing; fresh clears first (§9)."""
    place = Path(place_dir)
    place.mkdir(parents=True, exist_ok=True)
    occ_path = place / "occupancy.csv"
    wl_path = place / "watchlist.csv"

    new_bins = run_occupancy_pass(parse_rtl_power_csv(rtl_power_csv), margin_db)
    if fresh:
        bins = new_bins
    else:
        bins = accumulate(read_occupancy_csv(occ_path), new_bins)

    pins = read_watchlist_pins(wl_path)  # preserve user pins across re-run
    write_occupancy_csv(bins, occ_path)
    write_watchlist_csv(bins, wl_path, occ_cutoff=occ_cutoff, margin_db=margin_db, pins=pins)
    return bins


def read_watchlist_rows(path: str | Path) -> list[dict]:
    p = Path(path)
    if not p.exists():
        return []
    with p.open("r", newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def set_pin(place_dir: str | Path, freq_hz: int, source: str, *, modulation: str = "OOK",
            threshold_dbm: float = -90.0, occupancy: float = 0.0) -> None:
    """Pin or exclude a frequency (source=user-pin|user-exclude), persisted in watchlist.csv
    and preserved across re-runs/Reset (System §9)."""
    place = Path(place_dir)
    bins = read_occupancy_csv(place / "occupancy.csv")
    pins = [p for p in read_watchlist_pins(place / "watchlist.csv") if p.freq_hz != freq_hz]
    pins.append(Pin(freq_hz, modulation, threshold_dbm, occupancy, source))
    write_watchlist_csv(bins, place / "watchlist.csv", pins=pins)


def reset_place(place_dir: str | Path, keep_pins: bool = True) -> None:
    """Reset recon for a place: wipe occupancy.csv + watchlist.csv (keep or wipe user pins).
    Touches recon artifacts only — captures / signatures untouched (§9)."""
    place = Path(place_dir)
    occ_path = place / "occupancy.csv"
    wl_path = place / "watchlist.csv"
    pins = read_watchlist_pins(wl_path) if keep_pins else []

    write_occupancy_csv([], occ_path)
    write_watchlist_csv([], wl_path, pins=pins)
