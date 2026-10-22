import logging
import os
import secrets
from pathlib import Path
from fastapi import FastAPI, Depends, HTTPException, Security, Request, status
from fastapi.security import APIKeyHeader
from pydantic import BaseModel, Field

# Setup logging with security audit format
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s"
)
logger = logging.getLogger("SmartPatientMonitor.Security")

# Attempt to load .env file if available
try:
    from dotenv import load_dotenv
    env_path = Path(__file__).resolve().parent.parent / ".env"
    if env_path.exists():
        load_dotenv(dotenv_path=env_path)
        logger.info(f"Loaded environment variables from {env_path}")
except ImportError:
    pass

# Retrieve API Key from environment or use a secure default for development
EXPECTED_API_KEY = os.getenv("ESP32_API_KEY", "bme_secret_esp32_key_2026")

app = FastAPI(
    title="Smart Patient Monitor API (Secure)",
    version="0.2.0",
    description="Secure Biomedical Telemetry API for ESP32 with HTTPS and API Key Authentication."
)

# API Key Security Header Scheme
api_key_header = APIKeyHeader(name="X-API-Key", auto_error=False)


def verify_api_key(request: Request, api_key: str = Security(api_key_header)) -> str:
    """
    Validates the X-API-Key header sent by the ESP32 using constant-time comparison
    to protect against timing side-channel attacks.
    """
    client_ip = request.client.host if request.client else "unknown"

    if not api_key:
        logger.warning(
            f"[SECURITY AUDIT] Unauthorized access: Missing 'X-API-Key' header from IP: {client_ip} "
            f"on path: {request.url.path}"
        )
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Missing 'X-API-Key' authentication header."
        )

    # Constant-time comparison to prevent timing attacks
    if not secrets.compare_digest(api_key, EXPECTED_API_KEY):
        masked_key = api_key[:3] + "..." if len(api_key) > 3 else "***"
        logger.warning(
            f"[SECURITY AUDIT] Unauthorized access: Invalid API key ('{masked_key}') "
            f"from IP: {client_ip} on path: {request.url.path}"
        )
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid 'X-API-Key' authentication credential."
        )

    return api_key


class MeasurementInput(BaseModel):
    device_id: str = Field(min_length=1, max_length=64)
    heart_rate_bpm: int = Field(ge=0, le=300)
    spo2_percent: float = Field(ge=0, le=100)


@app.get("/")
def home():
    return {
        "message": "Smart Patient Monitor Backend is running securely",
        "https_enabled": True,
        "auth_required": "X-API-Key header required on /api/v1/measurements"
    }


@app.get("/health")
def health_check():
    return {
        "status": "ok"
    }


@app.post("/api/v1/measurements", dependencies=[Depends(verify_api_key)])
def receive_measurement(measurement: MeasurementInput, request: Request):
    client_ip = request.client.host if request.client else "unknown"
    logger.info(
        f"[TELEMETRY] Received verified data from device '{measurement.device_id}' "
        f"(IP: {client_ip}) | BPM: {measurement.heart_rate_bpm} | SpO2: {measurement.spo2_percent}%"
    )

    return {
        "status": "received",
        "message": "Measurement received and authenticated successfully",
        "data": measurement.model_dump(),
    }
