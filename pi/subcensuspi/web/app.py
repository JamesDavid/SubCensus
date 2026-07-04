"""FastAPI dashboard (Pi §7): live feed + device list + inline labeling + read-only JSON API.

Server-rendered (Jinja) + a little vanilla JS for live updates; no heavy SPA (keeps deps
light for a Pi). Every view has a matching JSON endpoint. A fresh SQLite connection is opened
per request (cheap, thread-safe) rather than sharing one across the threadpool.
"""

from __future__ import annotations

import os
from pathlib import Path

from fastapi import FastAPI, Form, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.templating import Jinja2Templates

from pathlib import Path as _Path

from ..db import Database
from ..occupancy_pass import (
    read_occupancy_csv,
    read_watchlist_rows,
    reset_place,
    set_pin,
)
from ..taxonomy import class_ids, is_valid

TEMPLATES = Jinja2Templates(directory=str(Path(__file__).parent / "templates"))

# Env var used by `uvicorn subcensuspi.web.app:app`; tests pass a path to create_app().
DB_PATH_ENV = "SUBCENSUSPI_DB"


def _row_to_dict(row) -> dict:
    return {k: row[k] for k in row.keys()}


def create_app(db_path: str, place: str | None = None, places_dir: str | None = None) -> FastAPI:
    app = FastAPI(title="SubCensusPi")
    app.state.db_path = db_path
    app.state.place = place
    app.state.places_dir = places_dir

    def get_db() -> Database:
        return Database(app.state.db_path)

    def place_dir(p: str | None) -> _Path | None:
        pl = p or app.state.place
        if not app.state.places_dir or not pl:
            return None
        return _Path(app.state.places_dir) / pl

    @app.get("/", response_class=HTMLResponse)
    def index(request: Request, place: str | None = None):
        db = get_db()
        try:
            p = place or app.state.place
            devices = [_row_to_dict(r) for r in db.list_devices(p)]
            events = [_row_to_dict(r) for r in db.recent_events(30, p)]
        finally:
            db.close()
        return TEMPLATES.TemplateResponse(
            request=request,
            name="index.html",
            context={
                "devices": devices,
                "events": events,
                "classes": class_ids(),
                "place": p,
            },
        )

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
            return [_row_to_dict(r) for r in db.recent_events(limit, place or app.state.place)]
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

    @app.post("/api/watchlist/pin")
    def api_pin(freq_hz: int = Form(...), action: str = Form(...), place: str = Form(default="")):
        d = place_dir(place or None)
        if d is None:
            raise HTTPException(status_code=400, detail="no places_dir configured")
        if action not in ("pin", "exclude"):
            raise HTTPException(status_code=400, detail="action must be pin|exclude")
        set_pin(d, freq_hz, source=f"user-{action}")
        return JSONResponse({"ok": True, "freq_hz": freq_hz, "action": action})

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


# ASGI entry for uvicorn.
app = create_app(
    os.environ.get(DB_PATH_ENV, "census.db"),
    place=os.environ.get("SUBCENSUSPI_PLACE"),
    places_dir=os.environ.get("SUBCENSUSPI_PLACES_DIR"),
)
