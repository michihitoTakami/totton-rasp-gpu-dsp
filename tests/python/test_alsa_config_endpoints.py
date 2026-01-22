from fastapi.testclient import TestClient

from web.main import app
from web.models import AlsaSettings, FilterSettings, Settings


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
            alsa=AlsaSettings(
                input_device="hw:0,0",
                output_device="hw:1,0",
                sample_rate=48000,
                channels=2,
                format="S32_LE",
                period_frames=4096,
                buffer_frames=16384,
            ),
            filter=FilterSettings(
                ratio=2,
                phase_type="minimum",
                directory="/opt/totton-dsp/data/coefficients",
            ),
        ),
    )

    client = TestClient(app)
    response = client.patch(
        "/api/config",
        json={
            "alsa": {
                "input_device": "hw:2,0",
                "output_device": "hw:3,0",
                "sample_rate": 96000,
                "channels": 4,
                "format": "S24_3LE",
                "period_frames": 2048,
                "buffer_frames": 8192,
            },
            "filter": {
                "ratio": 4,
                "phase_type": "linear",
                "directory": "/tmp/filters",
            },
        },
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["success"] is True
    assert payload["data"]["daemon_running"] is False
    assert payload["restart_required"] is True
