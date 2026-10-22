from fastapi.testclient import TestClient
from backend.app.main import app

client = TestClient(app)


def test_valid_measurement():
    response = client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "heart_rate_bpm": 75,
            "spo2_percent": 98,
        },
    )

    assert response.status_code == 200


def test_invalid_spo2():
    response = client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "heart_rate_bpm": 75,
            "spo2_percent": 150,
        },
    )

    assert response.status_code == 422


def test_invalid_heart_rate():
    response = client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "heart_rate_bpm": -5,
            "spo2_percent": 98,
        },
    )

    assert response.status_code == 422


def test_missing_heart_rate():
    response = client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "spo2_percent": 98,
        },
    )

    assert response.status_code == 422