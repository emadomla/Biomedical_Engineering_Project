
from fastapi import FastAPI
from pydantic import BaseModel, Field

app = FastAPI(
    title="Smart Patient Monitor API",
    version="0.1.0",
)


class MeasurementInput(BaseModel):
    device_id: str = Field(min_length=1, max_length=64)
    heart_rate_bpm: int = Field(ge=0, le=300)
    spo2_percent: float = Field(ge=0, le=100)


@app.get("/")
def home():
    return {
        "message": "Smart Patient Monitor Backend is running"
    }


@app.get("/health")
def health_check():
    return {
        "status": "ok"
    }


@app.post("/api/v1/measurements")
def receive_measurement(measurement: MeasurementInput):
    return {
        "status": "received",
        "message": "Measurement received successfully",
        "data": measurement.model_dump(),
    }
