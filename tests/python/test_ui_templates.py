from fastapi.testclient import TestClient

from web.main import app


def test_eq_settings_template_renders():
    client = TestClient(app)
    response = client.get("/")

    assert response.status_code == 200
    assert "Magic Box Control" in response.text
