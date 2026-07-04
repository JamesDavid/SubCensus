"""Occupancy accumulation + watchlist derivation — port of shared/core/sc_occupancy.c (System §9).

SubCensusPi's analog of the Zero's Recon Stage A: a different engine (wideband rtl_power sweep
vs stepped RSSI) producing the SAME `occupancy.csv` / `watchlist.csv` artifacts (Pi §3, §9a).
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass
class OccupancyBin:
    freq_hz: int
    noise_floor: float = 0.0
    peak_rssi: float = 0.0
    occupancy: float = 0.0
    crossings: int = 0
    last_seen: int = 0  # epoch seconds


class OccupancyAccum:
    """Online accumulator for one bin (threshold supplied per sample)."""

    def __init__(self, freq_hz: int):
        self.freq_hz = freq_hz
        self.peak_rssi = 0.0
        self.noise_floor = 0.0
        self.above = 0
        self.total = 0
        self.crossings = 0
        self.last_seen = 0
        self._prev_above = False
        self._started = False

    def sample(self, rssi_dbm: float, threshold_dbm: float, ts_s: int) -> None:
        if not self._started:
            self.peak_rssi = rssi_dbm
            self.noise_floor = rssi_dbm
            self._started = True
        else:
            self.peak_rssi = max(self.peak_rssi, rssi_dbm)
            self.noise_floor = min(self.noise_floor, rssi_dbm)
        self.total += 1
        above = rssi_dbm >= threshold_dbm
        if above:
            self.above += 1
            self.last_seen = ts_s
            if not self._prev_above:
                self.crossings += 1
        self._prev_above = above

    def finish(self) -> OccupancyBin:
        return OccupancyBin(
            freq_hz=self.freq_hz,
            peak_rssi=self.peak_rssi,
            noise_floor=self.noise_floor,
            occupancy=(self.above / self.total) if self.total else 0.0,
            crossings=self.crossings,
            last_seen=self.last_seen,
        )


def merge(acc: OccupancyBin, acc_weight: int, incoming: OccupancyBin, in_weight: int) -> None:
    """Accumulate `incoming` into `acc` across passes (System §9)."""
    acc_weight = max(0, acc_weight)
    in_weight = max(0, in_weight)
    total = acc_weight + in_weight
    acc.peak_rssi = max(acc.peak_rssi, incoming.peak_rssi)
    acc.crossings += incoming.crossings
    acc.last_seen = max(acc.last_seen, incoming.last_seen)
    if total > 0:
        acc.occupancy = (acc.occupancy * acc_weight + incoming.occupancy * in_weight) / total
        acc.noise_floor = (acc.noise_floor * acc_weight + incoming.noise_floor * in_weight) / total
    if acc.freq_hz == 0:
        acc.freq_hz = incoming.freq_hz


@dataclass
class WatchlistEntry:
    freq_hz: int
    modulation: str  # "" until Stage B resolves (Pi resolves via rtl_433 decode)
    threshold_dbm: float
    occupancy: float


def watchlist_from_occupancy(
    bins: list[OccupancyBin], occ_cutoff: float, margin_db: float
) -> list[WatchlistEntry]:
    """Derive watchlist entries (occupancy >= cutoff), sorted by occupancy desc (busiest first)."""
    entries = [
        WatchlistEntry(
            freq_hz=b.freq_hz,
            modulation="",
            threshold_dbm=b.noise_floor + margin_db,
            occupancy=b.occupancy,
        )
        for b in bins
        if b.occupancy >= occ_cutoff
    ]
    entries.sort(key=lambda e: e.occupancy, reverse=True)
    return entries
