"""UI routes."""

from pathlib import Path

from fastapi import APIRouter, Request
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates

from ..i18n import build_context

TEMPLATES = Jinja2Templates(directory=str(Path(__file__).parent.parent / "templates"))

router = APIRouter(tags=["ui"])


@router.get("/", response_class=HTMLResponse)
async def eq_settings(request: Request):
    """Render EQ settings page."""
    context = {"request": request, **build_context(request)}
    return TEMPLATES.TemplateResponse("pages/eq_settings.html", context)


@router.get("/settings", response_class=HTMLResponse)
async def alsa_settings(request: Request):
    """Render ALSA settings page."""
    context = {"request": request, **build_context(request)}
    return TEMPLATES.TemplateResponse("pages/settings.html", context)
