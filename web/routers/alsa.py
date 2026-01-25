"""ALSA device endpoints."""

import logging

from fastapi import APIRouter, HTTPException

from ..models import AlsaDeviceListResponse, AlsaDeviceOption
from ..services import get_daemon_client

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/alsa", tags=["alsa"])


@router.get("/devices", response_model=AlsaDeviceListResponse)
async def list_alsa_devices() -> AlsaDeviceListResponse:
    """List available ALSA playback/capture devices."""
    try:
        with get_daemon_client() as client:
            response = client.send_command("LIST_ALSA_DEVICES")
    except Exception as exc:
        logger.warning("Failed to fetch ALSA devices: %s", exc)
        raise HTTPException(status_code=502, detail="ALSA device query failed")

    if not response.success or not isinstance(response.data, dict):
        raise HTTPException(status_code=502, detail="ALSA device query failed")

    playback = response.data.get("playback", [])
    capture = response.data.get("capture", [])
    if not isinstance(playback, list) or not isinstance(capture, list):
        raise HTTPException(status_code=502, detail="Invalid ALSA device payload")

    def normalize(items: list[object]) -> list[AlsaDeviceOption]:
        options: list[AlsaDeviceOption] = []
        for item in items:
            if isinstance(item, dict):
                value = item.get("value")
                label = item.get("label")
                if value is None and label is not None:
                    value = label
                if label is None and value is not None:
                    label = value
                if value is None or label is None:
                    continue
                options.append(AlsaDeviceOption(value=str(value), label=str(label)))
            else:
                text = str(item)
                options.append(AlsaDeviceOption(value=text, label=text))
        return options

    return AlsaDeviceListResponse(
        playback=normalize(playback),
        capture=normalize(capture),
    )
