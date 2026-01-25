"""Daemon control endpoints."""

from fastapi import APIRouter, HTTPException

from ..models import ApiResponse, PhaseTypeResponse, PhaseTypeUpdateRequest
from ..services import get_daemon_client, restart_dsp_container

router = APIRouter(prefix="/daemon", tags=["daemon"])


@router.get("/phase-type", response_model=PhaseTypeResponse)
async def get_phase_type():
    """Get current phase type from daemon."""
    try:
        with get_daemon_client() as client:
            response = client.send_command("PHASE_TYPE_GET")
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc))
    if not response.success:
        raise HTTPException(status_code=502, detail=response.message)
    if not isinstance(response.data, dict) or "phase_type" not in response.data:
        raise HTTPException(status_code=502, detail="invalid phase response")
    return PhaseTypeResponse(phase_type=response.data["phase_type"])


@router.put("/phase-type", response_model=ApiResponse)
async def set_phase_type(request: PhaseTypeUpdateRequest):
    """Set phase type on daemon."""
    try:
        with get_daemon_client() as client:
            response = client.send_json(
                {"cmd": "PHASE_TYPE_SET", "phase_type": request.phase_type}
            )
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc))
    if not response.success:
        raise HTTPException(status_code=502, detail=response.message)
    data = response.data if isinstance(response.data, dict) else None
    return ApiResponse(
        success=True,
        message=f"Phase type set to {request.phase_type}",
        data=data,
    )


@router.post("/reload", response_model=ApiResponse)
async def reload_daemon():
    """Send RELOAD to daemon."""
    try:
        with get_daemon_client() as client:
            response = client.send_command("RELOAD")
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc))
    if not response.success:
        raise HTTPException(status_code=502, detail=response.message)
    return ApiResponse(success=True, message="Reload requested")


@router.post("/soft-reset", response_model=ApiResponse)
async def soft_reset_daemon():
    """Send SOFT_RESET to daemon."""
    try:
        with get_daemon_client() as client:
            response = client.send_command("SOFT_RESET")
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc))
    if not response.success:
        raise HTTPException(status_code=502, detail=response.message)
    return ApiResponse(success=True, message="Soft reset requested")


@router.post("/restart", response_model=ApiResponse)
async def restart_container():
    """Restart the DSP container to refresh device nodes."""
    try:
        restart_dsp_container()
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc))
    return ApiResponse(success=True, message="Restart requested")
