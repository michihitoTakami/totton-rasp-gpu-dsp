"""Docker control helpers for restarting containers."""

from __future__ import annotations

import socket
from pathlib import Path
from urllib.parse import quote

from ..constants import DOCKER_SOCKET_PATH, DSP_CONTAINER_NAME


class DockerControlError(RuntimeError):
    """Raised when Docker control commands fail."""


def _read_status_line(sock: socket.socket) -> tuple[int, str]:
    buffer = b""
    while b"\r\n" not in buffer:
        chunk = sock.recv(1024)
        if not chunk:
            break
        buffer += chunk
    line = buffer.split(b"\r\n", 1)[0].decode("ascii", errors="replace")
    if not line:
        raise DockerControlError("empty response from Docker socket")
    parts = line.split(" ", 2)
    if len(parts) < 2 or not parts[1].isdigit():
        raise DockerControlError(f"invalid Docker response: {line}")
    status_code = int(parts[1])
    reason = parts[2] if len(parts) > 2 else ""
    return status_code, reason


def _request_restart(socket_path: Path, container_name: str, timeout_s: float) -> None:
    if not socket_path.exists():
        raise DockerControlError(f"Docker socket not found at {socket_path}")
    encoded = quote(container_name, safe="")
    request = (
        f"POST /containers/{encoded}/restart HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode("ascii")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(timeout_s)
        sock.connect(str(socket_path))
        sock.sendall(request)
        status, reason = _read_status_line(sock)
    if status not in {200, 204}:
        raise DockerControlError(f"Docker restart failed: {status} {reason}".strip())


def restart_dsp_container(
    container_name: str | None = None,
    socket_path: Path | None = None,
    timeout_s: float = 2.0,
) -> None:
    """Restart the DSP container via the local Docker socket."""
    _request_restart(
        socket_path or DOCKER_SOCKET_PATH,
        container_name or DSP_CONTAINER_NAME,
        timeout_s,
    )
