"""Watchlist-driven attention priority (Pi §3, System §9a).

The Pi is wideband-continuous: with no watchlist it simply monitors its configured frequencies
(hop or multi-dongle) **unprioritized** — that is the floor and monitoring always runs
regardless (Pi §3, §49). The watchlist never *gates* reception; it only **reorders / assigns**
attention so hot bands are visited first (single-dongle hop) or handed a dedicated dongle
(multi-dongle). This is therefore strictly **opt-in** reordering over the unprioritized floor.

Pure data transform over `watchlist.csv` rows (freq_hz / occupancy) + the config dongles — no
hardware. RX-only: reordering a hop plan touches attention only, never transmits.
"""

from __future__ import annotations

from dataclasses import replace

from ..config import DongleConfig

# Match a configured freq to a watchlist entry within this tolerance (hopping/tuning slop).
DEFAULT_TOL_HZ = 250_000


def parse_freq_hz(spec: str | int | float) -> int:
    """Parse an rtl_433-style frequency ("433.92M", "915M", "315000000") to Hz."""
    if isinstance(spec, (int, float)):
        return int(spec)
    s = str(spec).strip().upper()
    mult = 1.0
    if s.endswith("M"):
        mult, s = 1_000_000.0, s[:-1]
    elif s.endswith("K"):
        mult, s = 1_000.0, s[:-1]
    elif s.endswith("G"):
        mult, s = 1_000_000_000.0, s[:-1]
    try:
        return int(round(float(s) * mult))
    except ValueError:
        return 0


def _occupancy_index(watchlist_rows: list[dict], tol_hz: int) -> list[tuple[int, float]]:
    """(freq_hz, occupancy) for non-excluded watchlist entries, hottest first."""
    hot: list[tuple[int, float]] = []
    for row in watchlist_rows:
        if str(row.get("source", "")).startswith("user-exclude"):
            continue  # an excluded band is de-prioritized, never boosted
        try:
            fhz = int(row.get("freq_hz", 0))
            occ = float(row.get("occupancy", 0.0) or 0.0)
        except (TypeError, ValueError):
            continue
        # a user pin is authoritative attention: float it to the top
        if str(row.get("source", "")).startswith("user-pin"):
            occ = max(occ, 1.0) + 1.0
        hot.append((fhz, occ))
    hot.sort(key=lambda t: t[1], reverse=True)
    return hot


def freq_score(freq_hz: int, hot: list[tuple[int, float]], tol_hz: int) -> float:
    """Occupancy of the nearest watchlist entry within tol_hz, else 0 (unknown = cold)."""
    best = 0.0
    for fhz, occ in hot:
        if abs(fhz - freq_hz) <= tol_hz and occ > best:
            best = occ
    return best


def prioritize_freqs(
    freqs: list[str], watchlist_rows: list[dict], *, tol_hz: int = DEFAULT_TOL_HZ
) -> list[str]:
    """Reorder a dongle's hop freqs so watchlist-hot bands come first (opt-in, §3).

    Stable: equal-score freqs keep their configured order, and freqs with no watchlist match
    keep the tail in their original order — so an empty/irrelevant watchlist is a no-op.
    """
    if not watchlist_rows:
        return list(freqs)
    hot = _occupancy_index(watchlist_rows, tol_hz)
    scored = [(f, freq_score(parse_freq_hz(f), hot, tol_hz)) for f in freqs]
    order = sorted(range(len(scored)), key=lambda i: (-scored[i][1], i))
    return [scored[i][0] for i in order]


def prioritize_dongles(
    dongles: list[DongleConfig], watchlist_rows: list[dict], *, tol_hz: int = DEFAULT_TOL_HZ
) -> list[DongleConfig]:
    """Opt-in attention assignment (§3): reorder each dongle's freqs by watchlist heat and
    order the dongles so the one covering the hottest band is serviced first (multi-dongle:
    "assign a dongle to a hot band"). Returns fresh DongleConfig copies; input untouched.

    No watchlist -> unchanged (the unprioritized floor).
    """
    if not watchlist_rows:
        return list(dongles)
    hot = _occupancy_index(watchlist_rows, tol_hz)
    out: list[tuple[float, int, DongleConfig]] = []
    for i, d in enumerate(dongles):
        new_freqs = prioritize_freqs(d.freqs, watchlist_rows, tol_hz=tol_hz)
        best = max((freq_score(parse_freq_hz(f), hot, tol_hz) for f in new_freqs), default=0.0)
        out.append((best, i, replace(d, freqs=new_freqs)))
    out.sort(key=lambda t: (-t[0], t[1]))  # hottest dongle first, else configured order
    return [d for _, _, d in out]
