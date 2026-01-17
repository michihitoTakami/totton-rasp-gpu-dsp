"""Status endpoints."""

from fastapi import APIRouter

from ..models import StatusResponse
from ..services import check_daemon_running, fetch_zmq_stats, load_stats

router = APIRouter(tags=["status"])


@router.get("/status", response_model=StatusResponse)
async def get_status():
    """Return minimal status for UI/API clients."""
    daemon_running = check_daemon_running()
    stats = load_stats()
    zmq_stats = fetch_zmq_stats() if daemon_running else {}

    return StatusResponse(
        daemon_running=daemon_running,
        state="running" if daemon_running else "stopped",
        input_rate=stats["input_rate"],
        output_rate=stats["output_rate"],
        xrun_total=stats["xrun_total"],
        phase_type=zmq_stats.get("phase_type"),
        uptime_ms=zmq_stats.get("uptime_ms"),
        reloads=zmq_stats.get("reloads"),
        soft_resets=zmq_stats.get("soft_resets"),
    )
