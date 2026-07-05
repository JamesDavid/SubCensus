#!/usr/bin/env python3
"""Generate PLACEHOLDER mockups of the SubCensusPi dashboard and SubCensusEsp web UI.

Dark-theme SVG mockups modelled from the real pages (the Pi dashboard was captured running against
fixture data; the Esp tabs from data/index.html) — NOT live captures. They render on GitHub and let
the READMEs show what the web UIs look like before a device is online. Regenerate:
`python docs/make_web_mockups.py`. Replace with real browser screenshots (same basename) any time.

Writes: pi/docs/screens/dashboard.svg, esp/docs/screens/{webui_live,webui_fieldmap,webui_settings}.svg
"""

from __future__ import annotations

import html
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BG, PANEL, BORDER = "#0d1117", "#161b22", "#30363d"
TXT, MUTED, ACCENT = "#e6edf3", "#8b949e", "#4493f8"
GREEN, RED, BTN, BTNTX = "#3fb950", "#f85149", "#21262d", "#c9d1d9"
FONT = "-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif"


class C:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.el = []

    def rect(self, x, y, w, h, fill=PANEL, stroke=None, rx=6):
        s = f' stroke="{stroke}"' if stroke else ""
        self.el.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"{s}/>')

    def t(self, x, y, s, size=13, fill=TXT, weight="400", anchor="start", mono=False):
        fam = "ui-monospace,Consolas,monospace" if mono else FONT
        self.el.append(
            f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" font-weight="{weight}" '
            f'text-anchor="{anchor}" font-family="{fam}">{html.escape(s)}</text>')

    def btn(self, x, y, label, w=None, warn=False):
        w = w or (12 + len(label) * 7)
        self.rect(x, y, w, 22, fill=BTN, stroke=(RED if warn else BORDER), rx=6)
        self.t(x + w / 2, y + 15, label, 12, RED if warn else BTNTX, anchor="middle")
        return w

    def inp(self, x, y, w, ph=""):
        self.rect(x, y, w, 18, fill=BG, stroke=BORDER, rx=4)
        if ph:
            self.t(x + 6, y + 13, ph, 11, MUTED)

    def spark(self, x, y, vals, h=16):
        bw = 4
        for i, v in enumerate(vals):
            bh = max(2, int(h * v))
            self.el.append(f'<rect x="{x+i*(bw+2)}" y="{y+h-bh}" width="{bw}" height="{bh}" '
                           f'fill="{GREEN}" rx="1"/>')

    def save(self, name):
        out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
               f'viewBox="0 0 {self.w} {self.h}">',
               f'<rect width="{self.w}" height="{self.h}" fill="{BG}"/>', *self.el,
               f'<text x="{self.w-10}" y="{self.h-8}" font-size="11" fill="{MUTED}" '
               f'text-anchor="end" font-family="{FONT}">placeholder mock</text>', "</svg>"]
        p = ROOT / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text("\n".join(out), encoding="utf-8")


def panel(c, x, y, w, h, title, sub=""):
    c.rect(x, y, w, h, fill=PANEL, stroke=BORDER)
    c.t(x + 14, y + 24, title, 15, ACCENT, "600")
    if sub:
        c.t(x + 14 + len(title) * 9 + 8, y + 24, sub, 11, MUTED)


def esp_header(c):
    c.t(20, 30, "SubCensusEsp", 20, TXT, "700")
    c.t(160, 30, "node online · place: home · mode: Camp · WiFi ok · CC1101 ok · TX: off", 12, MUTED)
    tabs = ["Live", "Review", "Bands", "Field-map", "Places", "Settings"]
    return tabs


def esp_tabs(c, active):
    x = 20
    for tb in ["Live", "Review", "Bands", "Field-map", "Places", "Settings"]:
        w = 20 + len(tb) * 8
        on = tb == active
        c.rect(x, 46, w, 26, fill=(ACCENT if on else BTN), stroke=BORDER, rx=6)
        c.t(x + w / 2, 63, tb, 12, ("#0d1117" if on else BTNTX), "600", "middle")
        x += w + 8


# --- Pi dashboard ---

def _waterfall(c, x, y, w, rows, cols):
    """A mock occupancy heatmap strip (top) + sweep waterfall (below), blue->red by 'power'."""
    import math
    cw = w / cols
    # heatmap strip
    for i in range(cols):
        t = 0.15 + 0.8 * (math.sin(i * 0.5) ** 2) * (0.4 + 0.6 * ((i * 31) % 7) / 7)
        c.el.append(f'<rect x="{x+i*cw:.1f}" y="{y}" width="{cw+0.6:.1f}" height="14" '
                    f'fill="hsl({(1-t)*240:.0f},85%,{28+t*30:.0f}%)"/>')
    # waterfall rows
    for r in range(rows):
        for i in range(cols):
            t = 0.1 + 0.85 * (math.sin(i * 0.5 + r * 0.3) ** 2) * (0.3 + 0.7 * ((i * 17 + r * 5) % 9) / 9)
            c.el.append(f'<rect x="{x+i*cw:.1f}" y="{y+18+r*6}" width="{cw+0.6:.1f}" height="6.4" '
                        f'fill="hsl({(1-t)*240:.0f},85%,{26+t*30:.0f}%)"/>')


def pi_dashboard():
    c = C(940, 660)
    c.t(20, 30, "SubCensusPi", 20, TXT, "700")
    c.t(150, 30, "place: all", 13, MUTED)
    c.t(20, 50, "Wideband RTL-SDR ISM census — RX only. 4 devices.", 12, MUTED)

    # Devices panel
    panel(c, 20, 66, 590, 320, "Devices")
    hx = [34, 120, 175, 235, 275, 360, 470, 505]
    for lbl, x in zip(["Model", "ID/Ch", "Band", "Count", "Last seen", "Activity", "SNR", "Label / Room / Class"], hx):
        c.t(x, 112, lbl, 11, MUTED, "600")
    rows = [("Acurite-Tower", "1234/A", "433.92", "4", "12:03:00", [.5, .7, .6, .9], "12.0"),
            ("Generic-Remote", "42", "433.92", "1", "12:00:20", [.9], "15.0"),
            ("Schrader", "9ABCDE", "315.00", "1", "12:00:10", [.6], "10.0"),
            ("Prologue-TH", "55/1", "433.92", "1", "12:00:05", [.5], "8.0")]
    for i, (m, idc, band, cnt, seen, spk, snr) in enumerate(rows):
        y = 140 + i * 58
        c.t(34, y, m, 12, TXT)
        c.t(120, y, idc, 12, TXT, mono=True)
        c.t(175, y, band, 12, TXT, mono=True)
        c.t(238, y, cnt, 12, TXT)
        c.t(275, y, seen, 11, MUTED, mono=True)
        c.spark(360, y - 12, spk)
        c.t(470, y, snr, 12, GREEN)
        c.inp(505, y - 12, 90, "label")
        c.inp(505, y + 8, 90, "room")
        c.rect(505, y + 28, 60, 18, fill=BG, stroke=BORDER, rx=4)
        c.t(511, y + 41, "class ▾", 11, MUTED)
        c.btn(575, y + 27, "save", 30)

    # Live feed
    panel(c, 630, 66, 290, 320, "Live feed")
    feed = [("12:03:00", "433.92", "Acurite-Tower · 12 dB"),
            ("12:02:00", "433.92", "Acurite-Tower · 12.5 dB"),
            ("12:01:00", "433.92", "Acurite-Tower · 11.5 dB"),
            ("12:00:20", "433.92", "Generic-Remote · 15 dB"),
            ("12:00:10", "315.00", "Schrader · 10 dB"),
            ("12:00:05", "433.92", "Prologue-TH · 8 dB"),
            ("12:00:00", "433.92", "Acurite-Tower · 12 dB")]
    for i, (ts, f, rest) in enumerate(feed):
        y = 116 + i * 26
        c.t(644, y, ts, 11, MUTED, mono=True)
        c.t(700, y, f, 11, ACCENT, mono=True)
        c.t(742, y, rest, 11, TXT)

    # Unknowns
    panel(c, 20, 400, 900, 68, "Unknowns", "review queue")
    for lbl, x in zip(["Time", "Band", "Pulse summary", "Sample", "Class"], [34, 150, 300, 640, 820]):
        c.t(x, 444, lbl, 11, MUTED, "600")
    c.t(34, 462, "No unknowns captured.", 12, MUTED)

    # Bands (recon controls + occupancy heatmap strip over a sweep waterfall)
    panel(c, 20, 482, 900, 160, "Bands", "occupancy heatmap & sweep waterfall")
    c.t(34, 524, "Recon:", 12, MUTED)
    x = 84
    x += c.btn(x, 514, "Run (Accumulate)") + 8
    x += c.btn(x, 514, "Run (Fresh)") + 8
    x += c.btn(x, 514, "Reset (keep pins)", warn=True) + 8
    c.btn(x, 514, "Reset (wipe pins)", warn=True)
    _waterfall(c, 34, 540, 872, 12, 120)
    c.t(34, 636, "300 MHz", 10, MUTED)
    c.t(470, 636, "occupancy heatmap (top) · waterfall dBm cold→hot", 10, MUTED, anchor="middle")
    c.t(906, 636, "930 MHz", 10, MUTED, anchor="end")
    c.save("pi/docs/screens/dashboard.svg")


# --- Esp web UI tabs ---

def esp_live():
    c = C(940, 380)
    esp_header(c)
    esp_tabs(c, "Live")
    panel(c, 20, 86, 900, 270, "Live feed")
    feed = [("433.92", "Acurite-Tower", "-61 dBm"), ("433.92", "Generic-Remote", "-55 dBm"),
            ("315.00", "Schrader", "-70 dBm"), ("433.92", "unknown", "-58 dBm"),
            ("433.92", "Prologue-TH", "-66 dBm")]
    for i, (f, m, r) in enumerate(feed):
        y = 132 + i * 30
        c.t(36, y, f + " MHz", 13, ACCENT, mono=True)
        c.t(150, y, m, 13, (MUTED if m == "unknown" else TXT))
        c.t(360, y, r, 13, GREEN, mono=True)
        c.t(460, y, "· logged, WebSocket push", 11, MUTED)
    c.save("esp/docs/screens/webui_live.svg")


def esp_fieldmap():
    c = C(940, 500)
    esp_header(c)
    esp_tabs(c, "Field-map")
    panel(c, 20, 86, 900, 400, "Field-map discovery", "System §7b — passive + guarded own-device TX")
    c.t(36, 138, "same-device .sub paths or hex frames:", 12, MUTED)
    c.inp(320, 126, 400, "captures/…_433.sub  (one per line)")
    c.t(36, 168, "signature:", 12, MUTED)
    c.inp(110, 156, 160, "unknown_433")
    x = 290
    x += c.btn(x, 154, "Analyze") + 10
    c.btn(x, 154, "Confirm → field_maps/")
    c.t(36, 208, "confidence 0.62 · checksum CRC-8", 12, GREEN)
    # segments table
    for lbl, xx in zip(["Segment", "bits", "class", "name", "semantics"], [36, 150, 220, 380, 560]):
        c.t(xx, 236, lbl, 11, MUTED, "600")
    segs = [("0..8", "8", "static", "id", ""), ("8..16", "8", "counter", "seq", ""),
            ("16..24", "8", "slow", "temp", "tracks temperature"), ("24..32", "8", "checksum", "crc", "")]
    for i, (b, n, cls, nm, sem) in enumerate(segs):
        y = 262 + i * 30
        c.t(36, y, b, 12, TXT, mono=True)
        c.t(150, y, n, 12, TXT)
        c.rect(215, y - 13, 90, 20, fill=BG, stroke=BORDER, rx=4)
        c.t(222, y, cls + " ▾", 12, TXT)
        c.inp(380, y - 13, 150, nm)
        c.inp(560, y - 13, 320, sem)
    # edit + tx
    c.t(36, 414, "edit frame:", 12, MUTED)
    c.inp(120, 402, 220, "A5 07 D6 3C")
    x = 360
    x += c.btn(x, 400, "Re-sign") + 10
    c.btn(x, 400, "Transmit 1 frame  (own-device, gated)", warn=True)
    c.t(36, 456, "Passive analysis — TX only fires on the explicit, TX-allow-list-gated button above.", 11, MUTED)
    c.save("esp/docs/screens/webui_fieldmap.svg")


def esp_settings():
    c = C(940, 430)
    esp_header(c)
    esp_tabs(c, "Settings")
    panel(c, 20, 86, 900, 330, "Settings")
    rows = [("Mode", "Camp ▾"), ("Freq list", "433.92 MHz ▾"), ("Capture", "Dual ▾"),
            ("Use watchlist", "on"), ("RSSI auto", "on"), ("Dwell ms", "80"),
            ("Survey minutes", "15"), ("Auto-classify", "on"), ("Match DB", "on"),
            ("TX enabled", "off"), ("WiFi SSID", "home-iot"), ("MQTT host", "192.168.1.10")]
    for i, (k, v) in enumerate(rows):
        col = i // 6
        row = i % 6
        x = 40 + col * 440
        y = 138 + row * 42
        c.t(x, y, k, 12, MUTED)
        c.rect(x + 150, y - 15, 230, 22, fill=BG, stroke=BORDER, rx=4)
        c.t(x + 160, y, v, 12, (GREEN if v == "on" else RED if v == "off" else TXT))
    c.btn(40, 380, "Save", 60)
    c.save("esp/docs/screens/webui_settings.svg")


if __name__ == "__main__":
    pi_dashboard()
    esp_live()
    esp_fieldmap()
    esp_settings()
    print("wrote pi/docs/screens/dashboard.svg + esp/docs/screens/webui_{live,fieldmap,settings}.svg")
