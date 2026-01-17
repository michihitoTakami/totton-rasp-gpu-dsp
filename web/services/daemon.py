"""Daemon control helpers."""

import json
from pathlib import Path

from ..constants import STATS_FILE_PATH
from .daemon_client import get_daemon_client


def check_daemon_running() -> bool:
    """Return True if the control daemon responds to PING."""
    try:
        with get_daemon_client(timeout_ms=500) as client:
            return client.ping()
    except Exception:
        return False


def load_stats(stats_path: Path | None = None) -> dict[str, int]:
    """Load runtime stats for UI display."""
    path = stats_path or STATS_FILE_PATH
    default_stats = {"input_rate": 0, "output_rate": 0, "xrun_total": 0}
    if not path.exists():
        return default_stats
    try:
        with open(path) as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError):
        return default_stats

    if not isinstance(data, dict):
        return default_stats

    audio = data.get("audio", {})
    if not isinstance(audio, dict):
        audio = {}

    audio_xrun = audio.get("xrun", {})
    if not isinstance(audio_xrun, dict):
        audio_xrun = {}

    xrun_total = int(
        audio_xrun.get("total", audio.get("xrun_count", data.get("xrun_count", 0)) or 0)
    )

    return {
        "input_rate": int(data.get("input_rate", 0) or 0),
        "output_rate": int(data.get("output_rate", 0) or 0),
        "xrun_total": xrun_total,
    }


def fetch_zmq_stats() -> dict:
    """Fetch stats over ZeroMQ if available."""
    try:
        with get_daemon_client(timeout_ms=500) as client:
            response = client.send_command("STATS")
        if response.success and isinstance(response.data, dict):
            return response.data
    except Exception:
        pass
    return {}
