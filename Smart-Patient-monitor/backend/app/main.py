"""
=====================================================================================
 PROJECT: Smart Patient Monitor - Secure FastAPI Backend
 FEATURE: Encrypted Telemetry Receiver with Constant-Time API Key Authentication
=====================================================================================

 👋 HELLO TEAM / COLLABORATOR!
 This is the central backend server that receives live patient vital signs from our
 ESP32 hardware (or simulator).

 Here is a quick guide to how security and data processing work in this file:

 1. AUTHENTICATION (X-API-Key Header):
    - Every incoming request to '/api/v1/measurements' MUST include an 'X-API-Key'
      header containing our shared secret password.
    - If the header is missing or incorrect, FastAPI immediately returns:
        HTTP 401 Unauthorized
    - We use 'secrets.compare_digest' instead of '==' to compare the key. This prevents
      hackers from guessing our password by measuring CPU execution time (Timing Attack).

 2. CONFIGURATION VIA .env FILE:
    - You can set 'ESP32_API_KEY' in the 'backend/.env' file.
    - If no .env file exists, it defaults to: 'bme_secret_esp32_key_2026'

 3. RUNNING WITH HTTPS:
    - To run securely with SSL encryption, generate local certs with:
        python generate_cert.py
    - Then launch the server with:
        uvicorn app.main:app --host 0.0.0.0 --port 8000 --ssl-keyfile key.pem --ssl-certfile cert.pem
=====================================================================================
"""

import logging
import os
import secrets
from pathlib import Path
from fastapi import FastAPI, Depends, HTTPException, Security, Request, status
from fastapi.security import APIKeyHeader
from pydantic import BaseModel, Field

# -----------------------------------------------------------------------------------
# 1. Logging Setup (Formats logs with timestamps and severity levels)
# -----------------------------------------------------------------------------------
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s"
)
logger = logging.getLogger("SmartPatientMonitor.Security")

# -----------------------------------------------------------------------------------
# 2. Environment Variables (.env) Loading
# -----------------------------------------------------------------------------------
# Attempts to load the secret API key from the local '.env' file if present
try:
    from dotenv import load_dotenv
    env_path = Path(__file__).resolve().parent.parent / ".env"
    if env_path.exists():
        load_dotenv(dotenv_path=env_path)
        logger.info(f"[CONFIG] Loaded secret configuration from: {env_path}")
    else:
        logger.info("[CONFIG] No .env file found. Using default development secret key.")
except ImportError:
    logger.info("[CONFIG] 'python-dotenv' not installed. Using default environment configuration.")

# Read the required API key (must match ESP32_API_KEY in ESP32 firmware)
EXPECTED_API_KEY = os.getenv("ESP32_API_KEY", "bme_secret_esp32_key_2026")

# -----------------------------------------------------------------------------------
# 3. FastAPI Application Initialization
# -----------------------------------------------------------------------------------
app = FastAPI(
    title="Smart Patient Monitor API (Secure)",
    version="0.2.0",
    description="Secure Biomedical Telemetry API for ESP32 with HTTPS and API Key Authentication."
)

# Define the security scheme: expects a header named 'X-API-Key'
api_key_header = APIKeyHeader(name="X-API-Key", auto_error=False)


# -----------------------------------------------------------------------------------
# 4. Security Dependency: API Key Verification Function
# -----------------------------------------------------------------------------------
def verify_api_key(request: Request, api_key: str = Security(api_key_header)) -> str:
    """
    FastAPI Dependency that checks every incoming request for a valid 'X-API-Key'.
    
    Why use secrets.compare_digest()?
    Standard string comparison ('key1 == key2') stops checking at the first mismatched
    character, creating tiny variations in response time. Hackers can exploit this 
    (Timing Attacks) to guess keys character-by-character. 
    'compare_digest' always takes the exact same time regardless of matches!
    """
    client_ip = request.client.host if request.client else "unknown"

    # Case 1: Header was completely missing
    if not api_key:
        logger.warning(
            f"[SECURITY AUDIT] Unauthorized attempt: Missing 'X-API-Key' header from IP: {client_ip} "
            f"on path: {request.url.path}"
        )
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Missing 'X-API-Key' authentication header."
        )

    # Case 2: Header was provided, but does not match our secret key
    if not secrets.compare_digest(api_key, EXPECTED_API_KEY):
        masked_key = api_key[:3] + "..." if len(api_key) > 3 else "***"
        logger.warning(
            f"[SECURITY AUDIT] Unauthorized attempt: Invalid API key ('{masked_key}') "
            f"from IP: {client_ip} on path: {request.url.path}"
        )
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid 'X-API-Key' authentication credential."
        )

    # Success: Key is verified
    return api_key


# -----------------------------------------------------------------------------------
# 5. Data Validation Schema (Pydantic Model)
# -----------------------------------------------------------------------------------
class MeasurementInput(BaseModel):
    """
    Validates incoming JSON telemetry sent from the ESP32.
    If the ESP32 sends invalid data (e.g. heart rate > 300), FastAPI rejects it automatically.
    """
    device_id: str = Field(min_length=1, max_length=64, description="Unique hardware identifier")
    heart_rate_bpm: int = Field(ge=0, le=300, description="Heart rate in beats per minute (0 = offline/sleep)")
    spo2_percent: float = Field(ge=0, le=100, description="Blood oxygen saturation percentage (0 = offline/sleep)")


# -----------------------------------------------------------------------------------
# 6. API Endpoints
# -----------------------------------------------------------------------------------

@app.get("/")
def home():
    """Public welcome route showing API security status."""
    return {
        "message": "Smart Patient Monitor Backend is running securely",
        "https_enabled": True,
        "auth_required": "X-API-Key header required on /api/v1/measurements"
    }


@app.get("/health")
def health_check():
    """Public health-check route (used by monitoring tools to check if backend is alive)."""
    return {
        "status": "ok"
    }


@app.post("/api/v1/measurements", dependencies=[Depends(verify_api_key)])
def receive_measurement(measurement: MeasurementInput, request: Request):
    """
    PROTECTED ENDPOINT: Receives authenticated vital signs from the ESP32.
    Only runs if 'verify_api_key' passes.
    """
    client_ip = request.client.host if request.client else "unknown"

    # Check for sleep/shutdown notification sent by the ESP32
    if measurement.heart_rate_bpm == 0 and measurement.spo2_percent == 0.0:
        logger.info(
            f"[PATIENT MONITOR] Device '{measurement.device_id}' (IP: {client_ip}) "
            f"entered DEEP SLEEP (Sensors detached / idle)."
        )
    else:
        logger.info(
            f"[TELEMETRY] Verified data from device '{measurement.device_id}' "
            f"(IP: {client_ip}) -> Heart Rate: {measurement.heart_rate_bpm} BPM | SpO2: {measurement.spo2_percent}%"
        )

    # Return confirmation back to the ESP32
    return {
        "status": "received",
        "message": "Measurement received and authenticated successfully",
        "data": measurement.model_dump(),
    }
