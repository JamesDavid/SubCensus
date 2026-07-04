"""Pulse-width clustering — Python port of shared/core/sc_pulse.c (System §7).

Greedy online clustering, mirrored from the C so cluster centers/counts match.
"""

from __future__ import annotations

from dataclasses import dataclass

_KMAX = 8
_ABS_FLOOR = 20


@dataclass
class PulseCluster:
    center_us: int
    count: int


def cluster(timings: list[int], rel_tol: float = 0.25, max_clusters: int = 3) -> list[PulseCluster]:
    """Cluster absolute pulse widths; return up to max_clusters sorted by count desc."""
    slots: list[dict] = []  # {sum, count, center}
    for t in timings:
        w = abs(t)
        if w == 0:
            continue
        best = -1
        best_d = 0
        for s in range(len(slots)):
            tol = int(rel_tol * slots[s]["center"])
            if tol < _ABS_FLOOR:
                tol = _ABS_FLOOR
            d = abs(w - slots[s]["center"])
            if d <= tol and (best < 0 or d < best_d):
                best = s
                best_d = d
        if best < 0:
            if len(slots) < _KMAX:
                slots.append({"sum": float(w), "count": 1, "center": w})
                continue
            # no free slot: assign to nearest
            nd = 0
            for s in range(len(slots)):
                d = abs(w - slots[s]["center"])
                if best < 0 or d < nd:
                    best = s
                    nd = d
        slots[best]["sum"] += w
        slots[best]["count"] += 1
        slots[best]["center"] = int(slots[best]["sum"] / slots[best]["count"])

    slots.sort(key=lambda s: s["count"], reverse=True)
    return [PulseCluster(s["center"], s["count"]) for s in slots[:max_clusters]]
