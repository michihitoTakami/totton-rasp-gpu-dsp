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
    if "alsa_input_device" in updates:
        config_updates["alsaInputDevice"] = updates["alsa_input_device"]
    if "alsa_output_device" in updates:
        config_updates["alsaOutputDevice"] = updates["alsa_output_device"]
    if "alsa_sample_rate" in updates:
        config_updates["alsaSampleRate"] = updates["alsa_sample_rate"]
    if "alsa_channels" in updates:
        config_updates["alsaChannels"] = updates["alsa_channels"]
    if "alsa_format" in updates:
        config_updates["alsaFormat"] = updates["alsa_format"]

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
