"""FastAPI dashboard (Pi §7): live feed + device list + inline labeling + read-only JSON API.

Server-rendered (Jinja) + a little vanilla JS for live updates; no heavy SPA (keeps deps
light for a Pi). Every view has a matching JSON endpoint. A fresh SQLite connection is opened
per request (cheap, thread-safe) rather than sharing one across the threadpool.
"""

from __future__ import annotations

import json
import os
from contextlib import asynccontextmanager
from datetime import datetime
from pathlib import Path

from fastapi import FastAPI, Form, HTTPException, Request
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse
from fastapi.templating import Jinja2Templates

from pathlib import Path as _Path

from ..db import Database
import subprocess

from ..live_sweep import LiveSweeper
from ..plausibility import assess
from ..radio import RadioManager
from ..readings import humanize_reading, raw_bits
from ..redecode import redecode_file, rtl433_available
from ..occupancy_pass import (
    SWEEP_PRESETS,
    read_occupancy_csv,
    read_spectrum_csv,
    read_watchlist_rows,
    reset_place,
    resolve_range,
    rtl_power_available,
    run_pass_to_place,
    set_pin,
    sweep_live_to_csv,
)
from ..taxonomy import class_ids, is_valid

TEMPLATES = Jinja2Templates(directory=str(Path(__file__).parent / "templates"))

# Env var used by `uvicorn subcensuspi.web.app:app`; tests pass a path to create_app().
DB_PATH_ENV = "SUBCENSUSPI_DB"

# Unicode-block sparkline ramp (low -> high). Rendered inline in the Devices table (Pi §7);
# self-contained (no SVG asset, no external JS/CDN) so the dashboard works fully offline.
_SPARK_BLOCKS = " ▁▂▃▄▅▆▇█"


def _row_to_dict(row) -> dict:
    return {k: row[k] for k in row.keys()}


def _parse_ts(ts: str) -> datetime | None:
    try:
        return datetime.fromisoformat(str(ts).replace("Z", "+00:00"))
    except (ValueError, TypeError):
        return None


def activity_buckets(timestamps: list[str], nbuckets: int = 24) -> list[int]:
    """Bucket a device's reception timestamps into `nbuckets` equal-time bins over its
    active span (first->last seen), returning per-bin reception counts. Drives the per-device
    activity sparkline (Pi §7), read straight from the `events` SQLite log."""
    parsed = sorted(t for t in (_parse_ts(x) for x in timestamps) if t is not None)
    counts = [0] * nbuckets
    if not parsed:
        return counts
    start = parsed[0].timestamp()
    span = parsed[-1].timestamp() - start
    if span <= 0:  # single reception (or all identical ts) -> newest bin
        counts[-1] = len(parsed)
        return counts
    for t in parsed:
        idx = int((t.timestamp() - start) / span * nbuckets)
        counts[min(idx, nbuckets - 1)] += 1
    return counts


def sparkline(counts: list[int]) -> str:
    """Render bucket counts as a unicode-block sparkline (Pi §7). Empty when no activity."""
    hi = max(counts) if counts else 0
    if hi == 0:
        return ""
    top = len(_SPARK_BLOCKS) - 1
    return "".join(_SPARK_BLOCKS[round(c / hi * top)] for c in counts)


def create_app(
    db_path: str,
    place: str | None = None,
    places_dir: str | None = None,
    config_path: str | None = None,
    radio_state_path: str | None = None,
) -> FastAPI:
    @asynccontextmanager
    async def lifespan(app: FastAPI):
        app.state.radio.resume()  # headless boot: re-apply the last selected mode (best-effort)
        yield
        try:
            app.state.radio.set_mode("off")  # clean teardown so the dongle is freed on restart
        except Exception:  # pragma: no cover
            pass

    app = FastAPI(title="SubCensusPi", lifespan=lifespan)
    app.state.db_path = db_path
    app.state.place = place
    app.state.places_dir = places_dir
    app.state.live = LiveSweeper()  # continuous live-spectrum streamer (Pi §7)
    # One web-controlled owner of the dongle: off / decode / spectrum (Pi §3, §9). The decode
    # collector runs as a managed subprocess; spectrum uses the LiveSweeper above.
    app.state.radio = RadioManager(
        app.state.live, config_path=config_path, state_path=radio_state_path
    )

    def get_db() -> Database:
        return Database(app.state.db_path)

    def place_dir(p: str | None) -> _Path | None:
        pl = p or app.state.place
        if not app.state.places_dir or not pl:
            return None
        return _Path(app.state.places_dir) / pl

    def samples_dir(p: str | None) -> _Path | None:
        """Where the collector's -S capture drops raw .cu8 bursts (the place iq dir, §6/§9a)."""
        d = place_dir(p)
        return (d / "iq") if d is not None else None

    def _sample_info(f: _Path) -> dict:
        st = f.stat()
        return {"file": f.name, "bytes": st.st_size,
                "mtime": datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds")}

    @app.get("/", response_class=HTMLResponse)
    def index(request: Request, place: str | None = None, show_all: bool = False):
        db = get_db()
        try:
            p = place or app.state.place
            all_devices = [_row_to_dict(r) for r in db.list_devices(p)]
            latest_raw = db.latest_raw_json_by_device(p)
            # Confidence & honesty gate (System §6): score each decode on physical plausibility +
            # corroboration; hide the low-confidence junk by default (e.g. an Opus at -40 °C null,
            # an Efergy at 96 A, a periodic sensor heard once). show_all=1 reveals everything.
            # Display-only — nothing is deleted or auto-relabeled (§6).
            assessments = {
                d["device_id"]: assess(latest_raw.get(d["device_id"]),
                                       model=d.get("model") or "", count=d.get("count") or 0)
                for d in all_devices
            }
            devices = all_devices if show_all else [
                d for d in all_devices if assessments[d["device_id"]].plausible
            ]
            hidden_count = len(all_devices) - len(devices)
            events = [_row_to_dict(r) for r in db.recent_events(30, p)]
            unknowns = [_row_to_dict(r) for r in db.list_unknowns(p)]
            # per-device activity sparkline from the events log (Pi §7), rendered server-side.
            sparklines = {
                d["device_id"]: sparkline(activity_buckets(db.device_event_timestamps(d["device_id"])))
                for d in devices
            }
            # latest decoded payload per device, humanized (temp/humidity/power/battery…) (Pi §7).
            readings = {
                d["device_id"]: humanize_reading(latest_raw.get(d["device_id"]))
                for d in devices
            }
            # per-device confidence + why-not reasons for the Confidence column (§6 honesty).
            confidence = {d["device_id"]: assessments[d["device_id"]].as_dict() for d in devices}
        finally:
            db.close()
        # Bands: occupancy heatmap + derived watchlist (Pi §7, §9a). Only surface bins that were
        # actually ACTIVE — a wall of 0%-occupancy bins is just the dongle's noise floor, not
        # signals, so it's dropped (that's the persistent "302–925 MHz rainbow" of nothing).
        d = place_dir(place)
        occupancy = []
        if d is not None:
            occupancy = [
                {"freq_hz": b.freq_hz, "noise_floor": b.noise_floor, "peak_rssi": b.peak_rssi,
                 "occupancy": b.occupancy, "crossings": b.crossings, "last_seen": b.last_seen}
                for b in read_occupancy_csv(d / "occupancy.csv")
                if (b.occupancy or 0) > 0 or (b.crossings or 0) > 0
            ]
            occupancy.sort(key=lambda r: r["occupancy"], reverse=True)
        has_activity = len(occupancy) > 0
        watchlist = read_watchlist_rows(d / "watchlist.csv") if d is not None else []
        # Waterfall: the downsampled rtl_power sweeps (freq x time, §7 tier 2). Don't paint the
        # static noise-floor rainbow when nothing was active — the canvas stays empty for LIVE use.
        spectrum = {"freqs": [], "sweeps": []}
        if d is not None and has_activity:
            bfreqs, rows = read_spectrum_csv(d / "spectrum.csv")
            spectrum = {"freqs": bfreqs,
                        "sweeps": [{"ts": ts, "dbm": dbm} for ts, dbm in rows]}
        return TEMPLATES.TemplateResponse(
            request=request,
            name="index.html",
            context={
                "devices": devices,
                "total_devices": len(all_devices),
                "hidden_count": hidden_count,
                "show_all": show_all,
                "events": events,
                "unknowns": unknowns,
                "sparklines": sparklines,
                "readings": readings,
                "confidence": confidence,
                "occupancy": occupancy,
                "watchlist": watchlist,
                "spectrum_json": json.dumps(spectrum),
                "classes": class_ids(),
                "place": p,
                "has_bands": d is not None,
            },
        )

    @app.get("/api/device/{device_id}/activity")
    def api_device_activity(device_id: str, buckets: int = 24):
        """Per-device activity sparkline data (Pi §7) — reception counts per time bin from the
        `events` log, plus the rendered unicode-block sparkline (handy for scripting)."""
        db = get_db()
        try:
            if db.get_device(device_id) is None:
                raise HTTPException(status_code=404, detail="device not found")
            counts = activity_buckets(db.device_event_timestamps(device_id), buckets)
        finally:
            db.close()
        return {"device_id": device_id, "buckets": counts, "sparkline": sparkline(counts)}

    # --- Captured bursts: raw .cu8 samples, re-testable against every decoder (System §6) ---

    @app.get("/api/samples")
    def api_samples(place: str | None = None, limit: int = 50):
        """The captured raw bursts (newest first) — each is a replayable .cu8 the collector saved
        via -S all. These are the received bits; the decode was only a guess about them."""
        d = samples_dir(place)
        if d is None or not d.is_dir():
            return []
        files = sorted(d.glob("*.cu8"), key=lambda f: f.stat().st_mtime, reverse=True)
        return [_sample_info(f) for f in files[:limit]]

    @app.post("/api/sample/redecode")
    def api_sample_redecode(file: str = Form(...), place: str = Form(default="")):
        """Replay one captured burst through EVERY decoder and return the ranked candidate list
        (System §6): "this signal matches these N things" — catalog hits marked, junk scored low.
        File replay never touches the dongle, so this works while live decode owns the radio."""
        d = samples_dir(place or None)
        if d is None:
            raise HTTPException(status_code=400, detail="no places_dir configured")
        path = d / _Path(file).name  # basename only — no path traversal
        if not path.is_file():
            raise HTTPException(status_code=404, detail="sample not found")
        if not rtl433_available():
            raise HTTPException(status_code=503,
                                detail="rtl_433 not installed — run pi/install.sh")
        db = get_db()
        try:
            candidates = redecode_file(path, db=db)
        except subprocess.TimeoutExpired:
            raise HTTPException(status_code=504, detail="rtl_433 replay timed out")
        except RuntimeError as e:
            raise HTTPException(status_code=503, detail=str(e))
        finally:
            db.close()
        return {"file": path.name, "candidates": candidates}

    def _rank_candidates(rows) -> list[dict]:
        """Rank co-burst decodes by the confidence gate (System §6) — the candidate fingerprints
        for one signal, best first."""
        out = []
        for r in rows:
            d = _row_to_dict(r)
            a = assess(d.get("raw_json"), model=d.get("model") or "", count=d.get("count") or 0)
            out.append({
                "device_id": d.get("device_id"),
                "model": d.get("model"),
                "dev_id": d.get("dev_id"),
                "channel": d.get("channel"),
                "freq_hz": d.get("freq_hz"),
                "reading": humanize_reading(d.get("raw_json")),
                "confidence": a.confidence,
                "plausible": a.plausible,
                "reasons": a.reasons,
            })
        out.sort(key=lambda c: c["confidence"], reverse=True)
        return out

    @app.get("/device/{device_id}", response_class=HTMLResponse)
    def device_detail(request: Request, device_id: str):
        """Per-device detail page (Pi §7): the full reception log ("every communication we've heard
        from this device") + the candidate fingerprints for its latest burst, ranked by confidence."""
        db = get_db()
        try:
            row = db.get_device(device_id)
            if row is None:
                raise HTTPException(status_code=404, detail="device not found")
            device = _row_to_dict(row)
            events = [_row_to_dict(e) for e in db.device_events_recent(device_id, limit=500)]
            latest = events[0] if events else None
            candidates = []
            if latest is not None:
                candidates = _rank_candidates(
                    db.signal_candidates(device.get("place"), latest.get("freq_hz") or 0,
                                         latest.get("ts") or "")
                )
        finally:
            db.close()
        assessment = assess((latest or {}).get("raw_json") if latest else None,
                            model=device.get("model") or "", count=device.get("count") or 0)
        for e in events:  # humanize each reception + expose its raw bits (the recoverable evidence)
            e["reading"] = humanize_reading(e.get("raw_json"))
            e["bits"] = raw_bits(e.get("raw_json"))
        any_bits = any(e["bits"] for e in events)
        # Captured raw bursts near this device's receptions (±3 s): the replayable evidence for
        # "match this signal against every decoder" (System §6). Empty until -S capture has run.
        samples = []
        sd = samples_dir(device.get("place"))
        if sd is not None and sd.is_dir():
            ev_epochs = [t.timestamp() for t in
                         (_parse_ts(e.get("ts") or "") for e in events[:100]) if t is not None]
            for f in sorted(sd.glob("*.cu8"), key=lambda f: f.stat().st_mtime, reverse=True)[:500]:
                if any(abs(f.stat().st_mtime - t) <= 3 for t in ev_epochs):
                    samples.append(_sample_info(f))
                if len(samples) >= 20:
                    break
        return TEMPLATES.TemplateResponse(
            request=request,
            name="device.html",
            context={
                "device": device,
                "events": events,
                "candidates": candidates,
                "assessment": assessment.as_dict(),
                "any_bits": any_bits,
                "samples": samples,
                "classes": class_ids(),
            },
        )

    @app.get("/api/device/{device_id}/events")
    def api_device_events(device_id: str, limit: int = 200):
        """Every reception logged for a device (Pi §7), newest first, each with its humanized
        decoded payload — the full "communications" history behind the detail page."""
        db = get_db()
        try:
            if db.get_device(device_id) is None:
                raise HTTPException(status_code=404, detail="device not found")
            rows = []
            for e in db.device_events_recent(device_id, limit):
                d = _row_to_dict(e)
                d["reading"] = humanize_reading(d.get("raw_json"))
                rows.append(d)
            return rows
        finally:
            db.close()

    @app.get("/api/device/{device_id}/candidates")
    def api_device_candidates(device_id: str):
        """The candidate fingerprints for this device's latest burst (System §6 multi-candidate):
        every decoder that matched the same time+frequency, ranked by the confidence gate."""
        db = get_db()
        try:
            row = db.get_device(device_id)
            if row is None:
                raise HTTPException(status_code=404, detail="device not found")
            device = _row_to_dict(row)
            events = db.device_events_recent(device_id, limit=1)
            if not events:
                return {"device_id": device_id, "candidates": []}
            latest = _row_to_dict(events[0])
            cands = _rank_candidates(
                db.signal_candidates(device.get("place"), latest.get("freq_hz") or 0,
                                     latest.get("ts") or "")
            )
        finally:
            db.close()
        return {"device_id": device_id, "ts": latest.get("ts"),
                "freq_hz": latest.get("freq_hz"), "candidates": cands}

    @app.get("/api/places")
    def api_places():
        """Distinct places present, plus the configured active place (Pi §9a)."""
        db = get_db()
        try:
            places = set(db.distinct_places())
        finally:
            db.close()
        if app.state.place:
            places.add(app.state.place)
        return {"active": app.state.place, "places": sorted(places)}

    @app.get("/api/devices")
    def api_devices(place: str | None = None):
        db = get_db()
        try:
            return [_row_to_dict(r) for r in db.list_devices(place or app.state.place)]
        finally:
            db.close()

    @app.get("/api/events")
    def api_events(limit: int = 50, place: str | None = None):
        db = get_db()
        try:
            rows = []
            for r in db.recent_events(limit, place or app.state.place):
                d = _row_to_dict(r)
                d["reading"] = humanize_reading(d.get("raw_json"))  # decoded payload, humanized
                rows.append(d)
            return rows
        finally:
            db.close()

    @app.get("/api/device/{device_id}")
    def api_device(device_id: str):
        db = get_db()
        try:
            row = db.get_device(device_id)
            if row is None:
                raise HTTPException(status_code=404, detail="device not found")
            return _row_to_dict(row)
        finally:
            db.close()

    # --- Bands / occupancy (Pi §7, §9a) ---

    @app.get("/api/occupancy")
    def api_occupancy(place: str | None = None):
        d = place_dir(place)
        if d is None:
            return []
        bins = read_occupancy_csv(d / "occupancy.csv")
        # ranked by occupancy desc (hot bins first)
        rows = [
            {"freq_hz": b.freq_hz, "noise_floor": b.noise_floor, "peak_rssi": b.peak_rssi,
             "occupancy": b.occupancy, "crossings": b.crossings, "last_seen": b.last_seen}
            for b in bins
        ]
        rows.sort(key=lambda r: r["occupancy"], reverse=True)
        return rows

    @app.get("/api/watchlist")
    def api_watchlist(place: str | None = None):
        d = place_dir(place)
        return read_watchlist_rows(d / "watchlist.csv") if d else []

    @app.get("/api/spectrum")
    def api_spectrum(place: str | None = None):
        """Waterfall data (Pi §7): the rolling window of downsampled rtl_power sweeps — a
        freq-bucket grid + the most recent sweeps' dBm rows (oldest first). Drives the occupancy
        heatmap + sweep waterfall in the Bands view. Empty until a recon pass has run."""
        d = place_dir(place)
        if d is None:
            return {"freqs": [], "sweeps": []}
        bfreqs, rows = read_spectrum_csv(d / "spectrum.csv")
        return {"freqs": bfreqs, "sweeps": [{"ts": ts, "dbm": dbm} for ts, dbm in rows]}

    @app.post("/api/watchlist/pin")
    def api_pin(freq_hz: int = Form(...), action: str = Form(...), place: str = Form(default="")):
        d = place_dir(place or None)
        if d is None:
            raise HTTPException(status_code=400, detail="no places_dir configured")
        if action not in ("pin", "exclude"):
            raise HTTPException(status_code=400, detail="action must be pin|exclude")
        set_pin(d, freq_hz, source=f"user-{action}")
        return JSONResponse({"ok": True, "freq_hz": freq_hz, "action": action})

    @app.get("/api/spectrum/live")
    def api_spectrum_live():
        """Live waterfall data (Pi §7): the in-memory ring of the most recent streaming sweeps.
        {running, range, error, freqs, sweeps[]}. Poll this ~1/s while live streaming is on."""
        return app.state.live.snapshot()

    @app.post("/api/spectrum/live")
    def api_spectrum_live_ctl(action: str = Form(...), band: str = Form(default="ism")):
        """Start/stop the continuous live sweep on the dongle (Pi §7). `band` is a preset
        (ism/315/433/915/full) or a raw low:high:bin. Routes through the radio manager so it takes
        the dongle exclusively — starting it stops decode; stopping it leaves the radio off."""
        radio = app.state.radio
        if action == "stop":
            # Resume whatever ran before the sweep (usually decode) — a spectrum look must not
            # silently leave the 24/7 census off (the mode is persisted across reboots).
            try:
                st = radio.set_mode(radio.after_spectrum_mode())
            except Exception:
                st = radio.set_mode("off")
            return {"running": False, "mode": st["mode"]}
        if action != "start":
            raise HTTPException(status_code=400, detail="action must be start|stop")
        try:
            radio.set_mode("spectrum", band=band)
        except FileNotFoundError as e:
            raise HTTPException(status_code=503, detail=str(e))
        return {"running": True, "range": resolve_range(band), "presets": list(SWEEP_PRESETS)}

    # --- Radio owner: one dongle, one mode (Pi §3, §9) ---

    @app.get("/api/radio")
    def api_radio_status():
        """Current radio state: {mode(off|decode|spectrum), band, error, decode{}, spectrum{}}.
        The dashboard Capture control polls this to show what the single dongle is doing."""
        return app.state.radio.status()

    @app.post("/api/radio")
    def api_radio_ctl(mode: str = Form(...), band: str = Form(default="")):
        """Switch the dongle between off / decode / spectrum (mutually exclusive — one radio).
        `decode` runs the collector (census); `spectrum` runs the live waterfall on `band`."""
        radio = app.state.radio
        try:
            return radio.set_mode(mode, band=band or None)
        except ValueError as e:
            raise HTTPException(status_code=400, detail=str(e))
        except FileNotFoundError as e:  # rtl_power missing (spectrum)
            raise HTTPException(status_code=503, detail=str(e))
        except RuntimeError as e:  # rtl_433 / config missing (decode)
            raise HTTPException(status_code=503, detail=str(e))

    @app.post("/api/recon/run")
    def api_run(
        place: str = Form(default=""),
        mode: str = Form(default="accumulate"),
        band: str = Form(default="ism"),
        duration_s: int = Form(default=20),
        rtl_power_csv: str = Form(default=""),
    ):
        """Run an occupancy/spectrum pass (Pi §7 Bands: Accumulate default / Fresh; cumulative per
        place, System §9). By DEFAULT this runs a LIVE `rtl_power` sweep on the dongle — the
        occupancy heatmap + waterfall come straight off the radio. (`rtl_power_csv=` is a dev-only
        override to replay a recorded sweep; not needed on real hardware.)"""
        d = place_dir(place or None)
        if d is None:
            raise HTTPException(status_code=400, detail="no places_dir configured")
        if mode not in ("accumulate", "fresh"):
            raise HTTPException(status_code=400, detail="mode must be accumulate|fresh")

        src = rtl_power_csv
        if not src:  # live sweep on the dongle (the normal path)
            if not rtl_power_available():
                raise HTTPException(
                    status_code=503,
                    detail="rtl_power not installed — run pi/install.sh (installs rtl-sdr).",
                )
            try:
                sweep_live_to_csv(
                    d / "recon_sweep.csv", freq_range=resolve_range(band), duration_s=duration_s
                )
            except FileNotFoundError as e:
                raise HTTPException(status_code=503, detail=str(e))
            except subprocess.TimeoutExpired:
                raise HTTPException(status_code=504, detail="rtl_power sweep timed out")
            except subprocess.CalledProcessError:
                raise HTTPException(
                    status_code=503,
                    detail="rtl_power sweep failed — is the dongle free? If the collector is "
                    "using it, stop it: sudo systemctl stop subcensuspi-collector. If you see "
                    "usb_claim_interface -6, the DVB driver holds it — pi/install.sh blacklists it.",
                )
            src = str(d / "recon_sweep.csv")
        elif not _Path(src).is_file():
            raise HTTPException(status_code=400, detail="rtl_power_csv not found")

        bins = run_pass_to_place(src, d, fresh=(mode == "fresh"))
        return JSONResponse({"ok": True, "mode": mode, "bins": len(bins), "live": not rtl_power_csv})

    @app.post("/api/recon/reset")
    def api_reset(place: str = Form(default=""), keep_pins: bool = Form(default=True)):
        d = place_dir(place or None)
        if d is None:
            raise HTTPException(status_code=400, detail="no places_dir configured")
        reset_place(d, keep_pins=keep_pins)
        return JSONResponse({"ok": True, "keep_pins": keep_pins})

    @app.get("/api/unknowns")
    def api_unknowns(place: str | None = None):
        db = get_db()
        try:
            return [_row_to_dict(r) for r in db.list_unknowns(place or app.state.place)]
        finally:
            db.close()

    @app.get("/api/unknown/{unknown_id}/inspect")
    def api_unknown_inspect(unknown_id: int):
        """Play/inspect an unknown from the review queue (Pi §6, §7): the pulse summary + the
        saved IQ sample's metadata and a download link. Inspecting the *recorded* sample is
        fully off-device (RF boundary); triggering a NEW live capture needs a dongle (TODO(hw))."""
        db = get_db()
        try:
            row = db.get_unknown(unknown_id)
            if row is None:
                raise HTTPException(status_code=404, detail="unknown not found")
            row = _row_to_dict(row)
        finally:
            db.close()
        iq_path = row.get("iq_path")
        iq_available = bool(iq_path) and _Path(iq_path).is_file()
        try:
            pulse = json.loads(row.get("pulse_summary") or "{}")
        except (json.JSONDecodeError, TypeError):
            pulse = {}
        return {
            "id": unknown_id,
            "ts": row.get("ts"),
            "freq_hz": row.get("freq_hz"),
            "source": row.get("source"),
            "iq_path": iq_path,
            "iq_available": iq_available,
            "iq_bytes": _Path(iq_path).stat().st_size if iq_available else 0,
            "pulse_summary": pulse,
            # download the recorded .cu8 IQ; re-run offline `rtl_433 -r <file> -A` to reclassify.
            "download_url": f"/api/unknown/{unknown_id}/iq" if iq_available else None,
        }

    @app.get("/api/unknown/{unknown_id}/iq")
    def api_unknown_iq(unknown_id: int):
        """Download the recorded IQ (`.cu8`) snippet for an unknown (Pi §4, §6). No live SDR:
        this serves the sample captured on-device earlier; capturing a new one is TODO(hw)."""
        db = get_db()
        try:
            row = db.get_unknown(unknown_id)
            if row is None:
                raise HTTPException(status_code=404, detail="unknown not found")
            iq_path = row["iq_path"]
        finally:
            db.close()
        if not iq_path or not _Path(iq_path).is_file():
            raise HTTPException(
                status_code=404,
                detail="no recorded IQ for this unknown (live capture requires a dongle: TODO(hw))",
            )
        return FileResponse(iq_path, media_type="application/octet-stream",
                            filename=_Path(iq_path).name)

    @app.post("/api/unknown/{unknown_id}/label")
    def api_unknown_label(
        unknown_id: int,
        device_class: str = Form(default=""),
        notes: str = Form(default=""),
    ):
        if not is_valid(device_class):
            raise HTTPException(status_code=400, detail=f"device_class must be one of {class_ids()} or empty")
        db = get_db()
        try:
            if db.get_unknown(unknown_id) is None:
                raise HTTPException(status_code=404, detail="unknown not found")
            db.set_unknown_label(unknown_id, device_class=device_class or None, notes=notes or None)
            return JSONResponse({"ok": True, "id": unknown_id})
        finally:
            db.close()

    @app.delete("/api/unknown/{unknown_id}")
    def api_unknown_discard(unknown_id: int):
        db = get_db()
        try:
            if not db.delete_unknown(unknown_id):
                raise HTTPException(status_code=404, detail="unknown not found")
            return JSONResponse({"ok": True, "id": unknown_id})
        finally:
            db.close()

    @app.get("/api/device/{device_id}/fieldmap")
    def api_fieldmap(device_id: str, hex_field: str = "data", ground_truth: str | None = None):
        """Passive field-map discovery proposal for a device (System §7b). Seeds the dashboard
        segment-labeling overlay. PROPOSAL only — RX-only, no active confirmation, user confirms."""
        from ..fieldmap import analyze_device, proposal_to_dict

        db = get_db()
        try:
            if db.get_device(device_id) is None:
                raise HTTPException(status_code=404, detail="device not found")
            proposal = analyze_device(db, device_id, hex_field=hex_field, ground_truth_field=ground_truth)
        finally:
            db.close()
        if proposal is None:
            raise HTTPException(status_code=422, detail="not enough frames / no raw payload for differential")
        return proposal_to_dict(proposal)

    @app.post("/api/device/{device_id}/label")
    def api_label(
        device_id: str,
        label: str = Form(default=""),
        room: str = Form(default=""),
        device_class: str = Form(default=""),
    ):
        if not is_valid(device_class):
            raise HTTPException(
                status_code=400,
                detail=f"device_class must be one of {class_ids()} or empty",
            )
        db = get_db()
        try:
            if db.get_device(device_id) is None:
                raise HTTPException(status_code=404, detail="device not found")
            db.set_device_label(
                device_id,
                label=label or None,
                room=room or None,
                device_class=device_class or None,
            )
            return JSONResponse({"ok": True, "device_id": device_id})
        finally:
            db.close()

    return app


# ASGI entry for uvicorn. SUBCENSUSPI_CONFIG points decode mode at the collector config;
# SUBCENSUSPI_RADIO_STATE persists the selected mode so a headless Pi resumes it on boot.
app = create_app(
    os.environ.get(DB_PATH_ENV, "census.db"),
    place=os.environ.get("SUBCENSUSPI_PLACE"),
    places_dir=os.environ.get("SUBCENSUSPI_PLACES_DIR"),
    config_path=os.environ.get("SUBCENSUSPI_CONFIG"),
    radio_state_path=os.environ.get("SUBCENSUSPI_RADIO_STATE"),
)
