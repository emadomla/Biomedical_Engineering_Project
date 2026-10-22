
from sqlalchemy import Column, DateTime, Float, Integer, String, func

from .database import Base


class Measurement(Base):
    __tablename__ = "measurements"

    id = Column(Integer, primary_key=True, index=True)

    device_id = Column(
        String(64),
        nullable=False,
        index=True,
    )

    heart_rate_bpm = Column(Integer, nullable=False)

    spo2_percent = Column(Float, nullable=False)

    created_at = Column(
        DateTime(timezone=True),
        nullable=False,
        server_default=func.current_timestamp(),
    )
