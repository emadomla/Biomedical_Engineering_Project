
import pytest
from fastapi.testclient import TestClient
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from backend.app.database import Base, get_db
from backend.app.main import app
from backend.app.models import Measurement


@pytest.fixture
def client(tmp_path):
    # Create a separate database for each test.
    db_path = (
        tmp_path / "test_smart_patient_monitor.db"
    ).as_posix()

    test_engine = create_engine(
        f"sqlite:///{db_path}",
        connect_args={"check_same_thread": False},
    )

    TestingSessionLocal = sessionmaker(
        autocommit=False,
        autoflush=False,
        bind=test_engine,
    )

    # Create the tables in the test database.
    Base.metadata.create_all(bind=test_engine)

    # Make the API use the test database instead
    # of the project's actual database.
    def override_get_db():
        db = TestingSessionLocal()
        try:
            yield db
        finally:
            db.close()

    app.dependency_overrides[get_db] = override_get_db

    try:
        with TestClient(app) as test_client:
            yield test_client
    finally:
        app.dependency_overrides.pop(get_db, None)
        test_engine.dispose()


def test_valid_measurement(client):
    response = client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "heart_rate_bpm": 75,
            "spo2_percent": 98,
        },
    )

    assert response.status_code == 200

    data = response.json()["data"]

    assert data["device_id"] == "esp32-001"
    assert data["heart_rate_bpm"] == 75
    assert data["spo2_percent"] == 98
    assert isinstance(data["id"], int)
    assert data["created_at"] is not None


def test_get_measurements_empty(client):
    response = client.get("/api/v1/measurements")

    assert response.status_code == 200

    result = response.json()

    assert result["count"] == 0
    assert result["data"] == []


def test_measurement_is_saved_and_returned_by_get(client):
    post_response = client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "heart_rate_bpm": 75,
            "spo2_percent": 98,
        },
    )

    assert post_response.status_code == 200

    saved_data = post_response.json()["data"]

    get_response = client.get("/api/v1/measurements")

    assert get_response.status_code == 200

    result = get_response.json()

    assert result["count"] == 1
    assert len(result["data"]) == 1
    assert result["data"][0]["id"] == saved_data["id"]
    assert result["data"][0]["device_id"] == "esp32-001"
    assert result["data"][0]["heart_rate_bpm"] == 75
    assert result["data"][0]["spo2_percent"] == 98


def test_invalid_spo2(client):
    response = client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "heart_rate_bpm": 75,
            "spo2_percent": 150,
        },
    )

    assert response.status_code == 422


def test_invalid_heart_rate(client):
    response = client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "heart_rate_bpm": -5,
            "spo2_percent": 98,
        },
    )

    assert response.status_code == 422


def test_missing_heart_rate(client):
    response = client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "spo2_percent": 98,
        },
    )

    assert response.status_code == 422


def test_filter_measurements_by_device_id(client):
    client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-001",
            "heart_rate_bpm": 75,
            "spo2_percent": 98,
        },
    )

    client.post(
        "/api/v1/measurements",
        json={
            "device_id": "esp32-002",
            "heart_rate_bpm": 80,
            "spo2_percent": 97,
        },
    )

    response = client.get(
        "/api/v1/measurements",
        params={"device_id": "esp32-001"},
    )

    assert response.status_code == 200
    result = response.json()

    assert result["count"] == 1
    assert result["data"][0]["device_id"] == "esp32-001"


def test_measurements_limit(client):
    for heart_rate in [70, 75, 80]:
        response = client.post(
            "/api/v1/measurements",
            json={
                "device_id": "esp32-001",
                "heart_rate_bpm": heart_rate,
                "spo2_percent": 98,
            },
        )
        assert response.status_code == 200

    response = client.get(
        "/api/v1/measurements",
        params={"limit": 2},
    )

    assert response.status_code == 200
    result = response.json()

    assert result["count"] == 2
    assert len(result["data"]) == 2
