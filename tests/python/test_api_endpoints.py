from fastapi.testclient import TestClient

from web.main import app


class _DummyClient:
    def __init__(self, send_command_response=None, send_json_response=None):
        self._send_command_response = send_command_response
        self._send_json_response = send_json_response

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False

    def send_command(self, _command):
        return self._send_command_response

    def send_json(self, _payload):
        return self._send_json_response


class _DummyResponse:
    def __init__(self, success, message, data=None):
        self.success = success
        self.message = message
        self.data = data


def test_api_status_uses_stats_and_zmq(monkeypatch):
    monkeypatch.setattr("web.routers.status.check_daemon_running", lambda: True)
    monkeypatch.setattr(
        "web.routers.status.load_stats",
        lambda: {"input_rate": 48000, "output_rate": 192000, "xrun_total": 2},
    )
    monkeypatch.setattr(
        "web.routers.status.fetch_zmq_stats",
        lambda: {"phase_type": "minimum", "uptime_ms": 1000, "reloads": 1},
    )

    client = TestClient(app)
    response = client.get("/api/status")

    assert response.status_code == 200
    payload = response.json()
    assert payload["daemon_running"] is True
    assert payload["input_rate"] == 48000
    assert payload["output_rate"] == 192000
    assert payload["xrun_total"] == 2
    assert payload["phase_type"] == "minimum"
    assert payload["uptime_ms"] == 1000
    assert payload["reloads"] == 1


def test_api_phase_type_get(monkeypatch):
    dummy = _DummyClient(
        send_command_response=_DummyResponse(
            True, "ok", data={"phase_type": "linear"}
        )
    )
    monkeypatch.setattr("web.routers.daemon.get_daemon_client", lambda: dummy)

    client = TestClient(app)
    response = client.get("/api/daemon/phase-type")

    assert response.status_code == 200
    assert response.json()["phase_type"] == "linear"


def test_api_phase_type_set(monkeypatch):
    dummy = _DummyClient(
        send_json_response=_DummyResponse(
            True, "ok", data={"phase_type": "minimum"}
        )
    )
    monkeypatch.setattr("web.routers.daemon.get_daemon_client", lambda: dummy)

    client = TestClient(app)
    response = client.put("/api/daemon/phase-type", json={"phase_type": "minimum"})

    assert response.status_code == 200
    body = response.json()
    assert body["success"] is True
    assert body["data"]["phase_type"] == "minimum"
