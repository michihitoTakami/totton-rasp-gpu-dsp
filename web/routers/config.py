"""Configuration endpoints."""

import logging

from fastapi import APIRouter, HTTPException

from ..models import ApiResponse, ConfigResponse, ConfigUpdateRequest
from ..services import (
    check_daemon_running,
    get_daemon_client,
    load_config,
    save_config_updates,
)

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/config", tags=["config"])


@router.get("", response_model=ConfigResponse)
async def get_config() -> ConfigResponse:
    """Return current config.json settings."""
    return ConfigResponse(settings=load_config())


@router.patch("", response_model=ApiResponse)
async def update_config(request: ConfigUpdateRequest) -> ApiResponse:
    """Update config.json and request streamer reload."""
    updates = request.model_dump(exclude_unset=True)
    config_updates: dict[str, object] = {}
    if "alsa" in updates and updates["alsa"] is not None:
        alsa_payload = updates["alsa"]
        config_updates["alsa"] = {
            "inputDevice": alsa_payload.get("input_device"),
            "outputDevice": alsa_payload.get("output_device"),
            "sampleRate": alsa_payload.get("sample_rate"),
            "channels": alsa_payload.get("channels"),
            "format": alsa_payload.get("format"),
            "periodFrames": alsa_payload.get("period_frames"),
            "bufferFrames": alsa_payload.get("buffer_frames"),
        }
    if "filter" in updates and updates["filter"] is not None:
        filter_payload = updates["filter"]
        config_updates["filter"] = {
            "ratio": filter_payload.get("ratio"),
            "phaseType": filter_payload.get("phase_type"),
            "directory": filter_payload.get("directory"),
        }

    if not save_config_updates(config_updates):
        raise HTTPException(status_code=500, detail="Failed to save config")

    settings = load_config()
    daemon_running = check_daemon_running()
    reload_success = False
    reload_message = None

    if daemon_running:
        try:
            with get_daemon_client() as client:
                result = client.send_command("RELOAD")
            reload_success = result.success
            if not result.success:
                reload_message = result.message
        except Exception as exc:
            reload_message = str(exc)
            logger.warning("Failed to request daemon reload: %s", reload_message)

    return ApiResponse(
        success=True,
        message="Config saved",
        data={
            "daemon_running": daemon_running,
            "daemon_reloaded": reload_success,
            "reload_error": reload_message,
            "settings": settings.model_dump(),
        },
        restart_required=not reload_success,
    )
