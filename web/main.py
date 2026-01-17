"""FastAPI app entrypoint for EQ management."""

from pathlib import Path

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles

from .routers import eq, ui

app = FastAPI(title="Totton EQ Control")

static_dir = Path(__file__).parent / "static"
app.mount("/static", StaticFiles(directory=str(static_dir)), name="static")

app.include_router(ui.router)
app.include_router(eq.router)
