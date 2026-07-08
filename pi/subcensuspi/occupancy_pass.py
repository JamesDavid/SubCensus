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
import shutil
import subprocess
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

from .dsp.occupancy import OccupancyBin, watchlist_from_occupancy

# Shared-schema headers (System §9). Kept in sync with shared/schema/*.schema.yaml.
OCCUPANCY_HEADER = ["freq_hz", "noise_floor", "peak_rssi", "occupancy", "crossings", "last_seen"]
WATCHLIST_HEADER = ["freq_hz", "modulation", "threshold_dbm", "occupancy", "source"]

DEFAULT_MARGIN_DB = 12.0
DEFAULT_OCC_CUTOFF = 0.10

# Sweep-history (waterfall, Pi §7 tier 2): downsample each sweep to a fixed freq-bucket grid and
# keep a rolling window of the most recent sweeps so the dashboard can stack them over time.
SPECTRUM_BUCKETS = 120
SPECTRUM_MAX_SWEEPS = 60
SPECTRUM_FLOOR_DBM = -120.0


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


# --- live rtl_power sweep (real hardware) ---

# CC1101-comparable ISM span; rtl_power sweeps it by retuning across the range.
DEFAULT_SWEEP_RANGE = "300M:928M:1M"


def rtl_power_available() -> bool:
    return shutil.which("rtl_power") is not None


def sweep_live_to_csv(
    out_path: str | Path,
    *,
    freq_range: str = DEFAULT_SWEEP_RANGE,
    integration_s: int = 1,
    duration_s: int = 20,
    device: str | int | None = None,
    timeout_s: int = 300,
) -> Path:
    """Run a REAL rtl_power sweep and write its CSV to out_path. Sweeps the ISM range for
    `duration_s` (multiple passes → real occupancy), integrating `integration_s` per line.
    Raises FileNotFoundError if rtl_power is missing, or subprocess errors on a busy/absent dongle
    (usb_claim_interface -6 => the dvb_usb_rtl28xxu kernel driver still holds the device: blacklist
    it; or the collector is using the dongle — stop it first)."""
    if not rtl_power_available():
        raise FileNotFoundError("rtl_power not found — install rtl-sdr (sudo apt install rtl-sdr)")
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    argv = ["rtl_power", "-f", freq_range, "-i", str(integration_s), "-e", str(duration_s)]
    if device not in (None, ""):
        argv += ["-d", str(device)]
    argv += [str(out_path)]
    subprocess.run(argv, check=True, timeout=timeout_s)
    return out_path


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


# --- sweep history for the waterfall (Pi §7 tier 2) ---

def sweeps_from_samples(samples):
    """Group flat (freq, dbm, ts, band) samples into per-timestamp sweeps (in first-seen order).
    Returns (ordered_ts, {ts: {freq: dbm}}, fmin, fmax)."""
    by_ts: dict[str, dict[int, float]] = {}
    order: list[str] = []
    fmin = fmax = None
    for freq_hz, dbm, ts, _band in samples:
        s = by_ts.get(ts)
        if s is None:
            s = by_ts[ts] = {}
            order.append(ts)
        # keep the strongest reading if a bin repeats within a sweep
        if freq_hz not in s or dbm > s[freq_hz]:
            s[freq_hz] = dbm
        fmin = freq_hz if fmin is None else min(fmin, freq_hz)
        fmax = freq_hz if fmax is None else max(fmax, freq_hz)
    return order, by_ts, fmin, fmax


def _bucket_freqs(fmin: int, fmax: int, n: int = SPECTRUM_BUCKETS) -> list[int]:
    if fmax <= fmin:
        return [fmin] * n
    step = (fmax - fmin) / n
    return [int(fmin + step * (i + 0.5)) for i in range(n)]


def bucket_sweep(freqmap: dict[int, float], bucket_freqs: list[int]) -> list[float]:
    """Peak-hold each sweep's readings into the bucket grid (dBm; SPECTRUM_FLOOR where empty)."""
    n = len(bucket_freqs)
    if n < 2:
        return [SPECTRUM_FLOOR_DBM] * n
    lo, hi = bucket_freqs[0], bucket_freqs[-1]
    span = hi - lo if hi > lo else 1
    out = [SPECTRUM_FLOOR_DBM] * n
    for f, dbm in freqmap.items():
        b = int((f - lo) / span * (n - 1) + 0.5)
        b = 0 if b < 0 else (n - 1 if b >= n else b)
        if dbm > out[b]:
            out[b] = dbm
    return out


def build_spectrum(samples, bucket_freqs: list[int] | None = None):
    """Downsample each sweep to the bucket grid. Returns (bucket_freqs, [(ts, [dbm,...]), ...])."""
    order, by_ts, fmin, fmax = sweeps_from_samples(samples)
    if not order:
        return (bucket_freqs or []), []
    if bucket_freqs is None:
        bucket_freqs = _bucket_freqs(fmin, fmax)
    rows = [(ts, bucket_sweep(by_ts[ts], bucket_freqs)) for ts in order]
    return bucket_freqs, rows


def read_spectrum_csv(path: str | Path):
    """Return (bucket_freqs, [(ts, [dbm,...]), ...]) or ([], []) if absent."""
    p = Path(path)
    if not p.exists():
        return [], []
    with p.open("r", newline="", encoding="utf-8") as fh:
        r = csv.reader(fh)
        header = next(r, None)
        if not header or len(header) < 2:
            return [], []
        bucket_freqs = [int(x) for x in header[1:]]
        rows = []
        for row in r:
            if len(row) < 2:
                continue
            rows.append((row[0], [float(x) for x in row[1:]]))
    return bucket_freqs, rows


def write_spectrum_csv(bucket_freqs, rows, path: str | Path,
                       max_sweeps: int = SPECTRUM_MAX_SWEEPS) -> None:
    rows = rows[-max_sweeps:]
    with Path(path).open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["ts", *bucket_freqs])
        for ts, dbms in rows:
            w.writerow([ts, *[round(d, 1) for d in dbms]])


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
    spec_path = place / "spectrum.csv"

    samples = list(parse_rtl_power_csv(rtl_power_csv))  # materialize: reused for occupancy + waterfall
    new_bins = run_occupancy_pass(samples, margin_db)
    if fresh:
        bins = new_bins
    else:
        bins = accumulate(read_occupancy_csv(occ_path), new_bins)

    pins = read_watchlist_pins(wl_path)  # preserve user pins across re-run
    write_occupancy_csv(bins, occ_path)
    write_watchlist_csv(bins, wl_path, occ_cutoff=occ_cutoff, margin_db=margin_db, pins=pins)

    # sweep-history waterfall (§7 tier 2): append this pass's sweeps to the rolling window,
    # aligning to the existing bucket grid on accumulate so old + new rows stack coherently.
    prev_freqs, prev_rows = ([], []) if fresh else read_spectrum_csv(spec_path)
    bucket_freqs, new_rows = build_spectrum(samples, prev_freqs or None)
    write_spectrum_csv(bucket_freqs, prev_rows + new_rows, spec_path)
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
    spec_path = place / "spectrum.csv"
    pins = read_watchlist_pins(wl_path) if keep_pins else []

    write_occupancy_csv([], occ_path)
    write_watchlist_csv([], wl_path, pins=pins)
    spec_path.unlink(missing_ok=True)  # clear the waterfall history too (recon artifact, §9)
