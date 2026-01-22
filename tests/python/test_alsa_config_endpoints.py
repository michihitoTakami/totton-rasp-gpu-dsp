from fastapi.testclient import TestClient

from web.main import app
from web.models import Settings


class _DummyClient:
    def __init__(self, send_command_response=None):
        self._send_command_response = send_command_response

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False

    def send_command(self, _command):
        return self._send_command_response


class _DummyResponse:
    def __init__(self, success, message, data=None):
        self.success = success
        self.message = message
        self.data = data


def test_api_list_alsa_devices(monkeypatch):
    dummy = _DummyClient(
        send_command_response=_DummyResponse(
            True, "ok", data={"playback": ["hw:0"], "capture": ["hw:1"]}
        )
    )
    monkeypatch.setattr("web.routers.alsa.get_daemon_client", lambda: dummy)

    client = TestClient(app)
    response = client.get("/api/alsa/devices")

    assert response.status_code == 200
    payload = response.json()
    assert payload["playback"] == ["hw:0"]
    assert payload["capture"] == ["hw:1"]


def test_api_update_config(monkeypatch):
    monkeypatch.setattr("web.routers.config.save_config_updates", lambda _u: True)
    monkeypatch.setattr("web.routers.config.check_daemon_running", lambda: False)
    monkeypatch.setattr(
        "web.routers.config.load_config",
        lambda: Settings(
            alsa_input_device="hw:0,0",
            alsa_output_device="hw:1,0",
            alsa_sample_rate=48000,
            alsa_channels=2,
            alsa_format="S32_LE",
        ),
    )

    client = TestClient(app)
    response = client.patch(
        "/api/config",
        json={
            "alsa_input_device": "hw:2,0",
            "alsa_output_device": "hw:3,0",
            "alsa_sample_rate": 96000,
            "alsa_channels": 4,
            "alsa_format": "S24_3LE",
        },
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["success"] is True
    assert payload["data"]["daemon_running"] is False
    assert payload["restart_required"] is True
