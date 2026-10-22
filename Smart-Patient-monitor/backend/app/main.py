
from fastapi import Depends, FastAPI, Query
from pydantic import BaseModel, Field
from sqlalchemy.orm import Session

from .database import Base, engine, get_db
from .models import Measurement


# Create the database table if it does not exist.
Base.metadata.create_all(bind=engine)


app = FastAPI(
    title="Smart Patient Monitor API",
    version="0.2.0",
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
def receive_measurement(
    measurement: MeasurementInput,
    db: Session = Depends(get_db),
):
    # Convert the validated input into a database record.
    db_measurement = Measurement(**measurement.model_dump())

    try:
        db.add(db_measurement)
        db.commit()
        db.refresh(db_measurement)
    except Exception:
        db.rollback()
        raise

    return {
        "status": "received",
        "message": "Measurement saved successfully",
        "data": {
            "id": db_measurement.id,
            "device_id": db_measurement.device_id,
            "heart_rate_bpm": db_measurement.heart_rate_bpm,
            "spo2_percent": db_measurement.spo2_percent,
            "created_at": db_measurement.created_at,
        },
    }



@app.get("/api/v1/measurements")
def list_measurements(
    device_id: str | None = None,
    limit: int = Query(default=100, ge=1, le=500),
    db: Session = Depends(get_db),
):
    query = db.query(Measurement)

    if device_id:
        query = query.filter(
            Measurement.device_id == device_id
        )

    records = (
        query
        .order_by(Measurement.id.desc())
        .limit(limit)
        .all()
    )

    return {
        "count": len(records),
        "data": [
            {
                "id": record.id,
                "device_id": record.device_id,
                "heart_rate_bpm": record.heart_rate_bpm,
                "spo2_percent": record.spo2_percent,
                "created_at": record.created_at,
            }
            for record in records
        ],
    }
