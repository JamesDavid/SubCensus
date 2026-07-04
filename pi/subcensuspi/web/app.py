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

from ..db import Database
from ..taxonomy import class_ids, is_valid

TEMPLATES = Jinja2Templates(directory=str(Path(__file__).parent / "templates"))

# Env var used by `uvicorn subcensuspi.web.app:app`; tests pass a path to create_app().
DB_PATH_ENV = "SUBCENSUSPI_DB"


def _row_to_dict(row) -> dict:
    return {k: row[k] for k in row.keys()}


def create_app(db_path: str, place: str | None = None) -> FastAPI:
    app = FastAPI(title="SubCensusPi")
    app.state.db_path = db_path
    app.state.place = place

    def get_db() -> Database:
        return Database(app.state.db_path)

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
app = create_app(os.environ.get(DB_PATH_ENV, "census.db"))
