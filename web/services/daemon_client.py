"""ZeroMQ client for communicating with the C++ control server."""

import json
import os
from dataclasses import dataclass

import zmq

from ..constants import ZEROMQ_IPC_PATH


def _get_default_endpoint() -> str:
    """Resolve default ZeroMQ endpoint using env overrides."""
    return (
        os.environ.get("TOTTON_ZMQ_ENDPOINT")
        or os.environ.get("ZMQ_ENDPOINT")
        or ZEROMQ_IPC_PATH
    )


@dataclass
class DaemonResponse:
    """Structured response from daemon."""

    success: bool
    message: str


class DaemonClient:
    """ZeroMQ client using REQ/REP pattern."""

    def __init__(self, endpoint: str | None = None, timeout_ms: int = 3000):
        self.endpoint = endpoint if endpoint is not None else _get_default_endpoint()
        self.timeout_ms = timeout_ms
        self._context: zmq.Context | None = None
        self._socket: zmq.Socket | None = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False

    def _ensure_connected(self) -> zmq.Socket:
        if self._socket is None:
            self._context = zmq.Context()
            self._socket = self._context.socket(zmq.REQ)
            self._socket.setsockopt(zmq.RCVTIMEO, self.timeout_ms)
            self._socket.setsockopt(zmq.SNDTIMEO, self.timeout_ms)
            self._socket.setsockopt(zmq.LINGER, 0)
            self._socket.connect(self.endpoint)
        return self._socket

    def close(self):
        if self._socket:
            self._socket.close()
            self._socket = None
        if self._context:
            self._context.term()
            self._context = None

    def send_command(self, command: str) -> DaemonResponse:
        socket = self._ensure_connected()
        socket.send_string(command)
        response = socket.recv_string()
        return self._parse_response(response)

    @staticmethod
    def _parse_response(response: str) -> DaemonResponse:
        """Parse daemon response (JSON status or legacy text)."""
        try:
            data = json.loads(response)
        except json.JSONDecodeError:
            return DaemonResponse(success=response.startswith("OK"), message=response)

        if isinstance(data, dict) and data.get("status") == "ok":
            return DaemonResponse(success=True, message=response)
        return DaemonResponse(success=False, message=response)

    def reload_config(self) -> tuple[bool, str]:
        result = self.send_command("RELOAD")
        return result.success, result.message

    def ping(self) -> bool:
        result = self.send_command("PING")
        return result.success


def get_daemon_client(
    endpoint: str | None = None, timeout_ms: int = 3000
) -> DaemonClient:
    """Factory for DaemonClient."""
    return DaemonClient(endpoint=endpoint, timeout_ms=timeout_ms)
