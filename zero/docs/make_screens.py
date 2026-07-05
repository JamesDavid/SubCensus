#!/usr/bin/env python3
"""Generate PLACEHOLDER mockups of every SubCensusZero screen (zero/docs/screens/*.svg).

These are hand-modelled from the scene draw code (128x64 mono), NOT real device captures —
they show the intended layout so the README reads correctly before we can run the FAP and take
qFlipper screenshots. Regenerate: `python zero/docs/make_screens.py`. Replace each with a real
screenshot (same filename) once hardware is available.
"""

from __future__ import annotations

import html
from pathlib import Path

OUT = Path(__file__).resolve().parent / "screens"
S = 4  # scale (128x64 -> 512x256)
W, H = 128, 64
PAD = 10
CAPH = 26
SCREEN = "#FF8200"  # Flipper orange backlight
INK = "#000000"
BEZEL = "#1a1a1a"
FONT = "'DejaVu Sans Mono','Consolas',monospace"


def _x(v):
    return PAD + v * S


def _y(v):
    return PAD + v * S


class Canvas:
    def __init__(self):
        self.el: list[str] = []

    def text(self, x, y, s, size=7, white=False, anchor="start"):
        self.el.append(
            f'<text x="{_x(x):.0f}" y="{_y(y):.0f}" font-size="{size*S*0.72:.0f}" '
            f'fill="{"#FF8200" if white else INK}" text-anchor="{anchor}" '
            f'font-family="{FONT}">{html.escape(s)}</text>')

    def frame(self, x, y, w, h):
        self.el.append(
            f'<rect x="{_x(x):.0f}" y="{_y(y):.0f}" width="{w*S}" height="{h*S}" '
            f'fill="none" stroke="{INK}" stroke-width="{max(1,S//2)}"/>')

    def box(self, x, y, w, h):
        self.el.append(
            f'<rect x="{_x(x):.0f}" y="{_y(y):.0f}" width="{w*S}" height="{h*S}" fill="{INK}"/>')

    def line(self, x1, y1, x2, y2):
        self.el.append(
            f'<line x1="{_x(x1):.0f}" y1="{_y(y1):.0f}" x2="{_x(x2):.0f}" y2="{_y(y2):.0f}" '
            f'stroke="{INK}" stroke-width="{max(1,S//2)}"/>')


def render(name: str, caption: str, c: Canvas):
    width = W * S + PAD * 2
    height = H * S + PAD * 2 + CAPH
    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        f'<rect width="{width}" height="{height}" fill="{BEZEL}" rx="8"/>',
        f'<rect x="{PAD}" y="{PAD}" width="{W*S}" height="{H*S}" fill="{SCREEN}"/>',
        *c.el,
        f'<text x="{width/2:.0f}" y="{height-8}" font-size="15" fill="#bbbbbb" '
        f'text-anchor="middle" font-family="{FONT}">placeholder mock · {html.escape(caption)}</text>',
        "</svg>",
    ]
    (OUT / f"{name}.svg").write_text("\n".join(svg), encoding="utf-8")


# --- reusable widgets ---

def submenu(header, items, sel=0):
    c = Canvas()
    if header:
        c.text(2, 9, header, size=8)
        top = 14
    else:
        top = 2
    rowh = 12
    for i, it in enumerate(items[:4]):
        y = top + i * rowh
        if i == sel:
            c.box(0, y, 128, rowh)
            c.text(3, y + 9, it, size=7, white=True)
        else:
            c.text(3, y + 9, it, size=7)
    return c


def varlist(rows, sel=0):
    c = Canvas()
    rowh = 13
    for i, (label, val) in enumerate(rows[:4]):
        y = 2 + i * rowh
        if i == sel:
            c.box(0, y, 128, rowh)
            c.text(3, y + 10, label, size=7, white=True)
            c.text(125, y + 10, val, size=7, white=True, anchor="end")
        else:
            c.text(3, y + 10, label, size=7)
            c.text(125, y + 10, val, size=7, anchor="end")
    return c


def dialog(header, lines, left, right):
    c = Canvas()
    c.text(64, 10, header, size=8, anchor="middle")
    for i, ln in enumerate(lines):
        c.text(64, 26 + i * 10, ln, size=7, anchor="middle")
    # buttons
    c.box(2, 52, 40, 11)
    c.text(22, 60, left, size=7, white=True, anchor="middle")
    c.box(86, 52, 40, 11)
    c.text(106, 60, right, size=7, white=True, anchor="middle")
    return c


# --- screens ---

def build_all():
    OUT.mkdir(parents=True, exist_ok=True)

    render("01_main_menu", "Main menu", submenu("", [
        "Place: Home", "Run Recon", "Recon results", "Start Sweep"], sel=1))

    render("02_settings", "Settings (§4)", varlist([
        ("Mode", "Camp"), ("Freq preset", "US ISM"),
        ("Edit custom list", "2 freqs"), ("Recon grid", "Hybrid")], sel=0))

    render("03_places", "Places", submenu("Places", [
        "* Home", "Truck", "Office", "+ New place"], sel=0))

    render("04_recon_run", "Run Recon", submenu("Run Recon", [
        "Accumulate", "Fresh"], sel=0))

    # Recon live — spectrum strip
    c = Canvas()
    c.frame(0, 2, 128, 38)
    import math
    for i in range(60):
        h = int(6 + 26 * abs(math.sin(i * 0.5)) * (0.3 + 0.7 * ((i * 37) % 5) / 5))
        x = 2 + i * 2
        c.box(x, 2 + 38 - 1 - h, 1, h)
    c.line(70, 2, 70, 40)  # cursor
    c.text(3, 12, "300-348 MHz", size=7)
    c.text(2, 50, "433.92  hot 7", size=7)
    c.text(2, 62, "pass 12  180s  Back:stop", size=7)
    render("05_recon_spectrum", "Recon live: spectrum strip", c)

    render("06_recon_hits", "Recon live: top hits (OK toggles)", Canvas())
    c = Canvas()
    c.text(2, 8, "Top hits (OK: spectrum)", size=7)
    for i, (f, r) in enumerate([("433.92", -52), ("315.00", -61), ("915.00", -70), ("390.00", -74)]):
        c.text(2, 18 + i * 8, f"{f} MHz  {r}", size=7)
    render("06_recon_hits", "Recon live: top hits (OK toggles)", c)

    render("07_recon_results", "Recon results", submenu("Recon results", [
        "433.92 42% ", "315.00 18% PIN", "915.00 9% EXCL", "Reset recon"], sel=0))

    render("08_recon_entry", "Recon entry actions", submenu("433.92 MHz", [
        "Pin", "Exclude", "Camp here"], sel=0))

    render("09_recon_reset", "Reset recon (keep/wipe pins)",
           dialog("Reset recon?", ["Clears occupancy +", "watchlist for this place.",
                                    "Keep your pins?"], "Keep pins", "Wipe pins"))

    render("10_camp_picker", "Camp frequency picker", submenu("Camp frequency", [
        "Auto (busiest watchlist)", "WL 433.92 MHz", "315.00 MHz", "Manual entry..."], sel=0))

    # Camp live view
    c = Canvas()
    c.text(2, 11, "CAMP", size=8)
    c.text(52, 11, "433.92 MHz", size=7)
    c.frame(2, 20, 124, 10)
    c.box(3, 21, 78, 8)
    c.text(2, 42, "RSSI -58 dBm", size=7)
    c.text(2, 54, "Hits: 12", size=7)
    c.text(70, 54, "OK: hits", size=7)
    c.text(2, 63, "Back: stop", size=7)
    render("11_camp_live", "Camp/Sweep live view", c)

    # Recent hits list
    c = Canvas()
    c.text(2, 8, "Hits OK:Review Back:live", size=7)
    rows = [("433.92", -52, "AcmeWx"), ("315.00", -61, "unknown"),
            ("433.92", -55, "PT2262"), ("915.00", -70, "unknown")]
    for i, (f, r, m) in enumerate(rows):
        y = 18 + i * 9
        if i == 0:
            c.box(0, y - 8, 128, 9)
            c.text(2, y, f"{f} {r} {m}", size=7, white=True)
        else:
            c.text(2, y, f"{f} {r} {m}", size=7)
    render("12_recent_hits", "Live recent-hits list", c)

    render("13_sweep_hint", "Sweep no-recon hint (§3.1)",
           dialog("No recon here", ["Sweeping the default", "band list. Run Recon",
                                    "to focus revisit."], "Run Recon", "Proceed"))

    render("14_review", "Review captures", submenu("Review captures", [
        "433.92 AcmeWx", "315.00 unknown", "433.92 garage", "915.00 unknown"], sel=0))

    render("15_review_detail", "Capture detail + candidates", submenu("433.92 AcmeWx 82%", [
        "Label device", "Replay to identify", "Edit / analyze"], sel=0))

    render("16_review_label", "Label picker (Accept/taxonomy)", submenu("Label device", [
        "Accept: weather", "garage", "car-fob", "tpms"], sel=0))

    render("17_replay_confirm", "Replay confirm (No defaulted)",
           dialog("Replay?", ["Send 433.92 MHz", "OOK x1 frame", "to identify your device?"],
                  "No", "Send"))

    render("18_edit_menu", "Edit / field-map menu (M10)", submenu("Edit (32 bits)", [
        "Raw bit/hex edit", "Structured fields", "Field-map discovery", "Send edited frame"], sel=0))

    # Edit raw hex
    c = Canvas()
    c.text(2, 8, "Raw hex  Up/Dn:byte L/R:+-", size=7)
    vals = ["A5", "3C", "10", "B9"]
    for i, v in enumerate(vals):
        x = 2 + i * 15
        if i == 0:
            c.box(x - 1, 14, 14, 10)
            c.text(x, 22, v, size=7, white=True)
        else:
            c.text(x, 22, v, size=7)
    c.text(2, 62, "byte 0 = 165", size=7)
    render("19_edit_raw", "Raw bit/hex editor", c)

    # Edit fields
    c = Canvas()
    c.text(2, 8, "Fields L/R:value", size=7)
    fields = [("id=165 static", True), ("chan=2 slow", False),
              ("temp=214 slow", False), ("crc=90 checksum", False)]
    for i, (t, sel) in enumerate(fields):
        y = 18 + i * 9
        if sel:
            c.box(0, y - 8, 128, 9)
            c.text(2, y, t, size=7, white=True)
        else:
            c.text(2, y, t, size=7)
    c.text(2, 62, "CRC auto-recomputed", size=7)
    render("20_edit_fields", "Structured field editor", c)

    # Edit discovery segments
    c = Canvas()
    c.text(2, 8, "Segments L/R:class", size=7)
    segs = [("byte0 [0:8] static", True), ("byte1 [8:8] counter", False),
            ("byte2 [16:8] slow", False), ("byte3 [24:8] checksum", False)]
    for i, (t, sel) in enumerate(segs):
        y = 18 + i * 9
        if sel:
            c.box(0, y - 8, 128, 9)
            c.text(2, y, t, size=7, white=True)
        else:
            c.text(2, y, t, size=7)
    render("21_edit_discovery", "Field-map discovery (differential)", c)

    render("22_sd_required", "SD card required (§6.1)", submenu("SD card required", [
        "About"], sel=0))

    # About (text scroll)
    c = Canvas()
    for i, ln in enumerate([
            "SubCensusZero v0.1   API 87.1", "Monitoring is passive:",
            "Sweep/Camp/Recon never TX.", "Replay & Edit-TX DO transmit",
            "(explicit, TX-allow-list).", "Storage: /ext SD 7100/7600 MB",
            "Battery ~5-8 h Camp/Sweep"]):
        c.text(2, 9 + i * 8, ln, size=7)
    render("23_about", "About", c)

    print(f"wrote {len(list(OUT.glob('*.svg')))} screen mockups to {OUT}")


if __name__ == "__main__":
    build_all()
