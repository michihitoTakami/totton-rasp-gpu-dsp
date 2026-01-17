"""Daemon control helpers."""

from .daemon_client import get_daemon_client


def check_daemon_running() -> bool:
    """Return True if the control daemon responds to PING."""
    try:
        with get_daemon_client(timeout_ms=500) as client:
            return client.ping()
    except Exception:
        return False
