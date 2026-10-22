
from pathlib import Path

from sqlalchemy import create_engine
from sqlalchemy.orm import declarative_base, sessionmaker


# Store the SQLite database beside this file.
DATABASE_PATH = (
    Path(__file__).resolve().parent / "smart_patient_monitor.db"
)

DATABASE_URL = f"sqlite:///{DATABASE_PATH.as_posix()}"


# Create the database engine.
engine = create_engine(
    DATABASE_URL,
    connect_args={"check_same_thread": False},
)


# Create a database session for each request.
SessionLocal = sessionmaker(
    autocommit=False,
    autoflush=False,
    bind=engine,
)


# Base class for database models.
Base = declarative_base()


def get_db():
    db = SessionLocal()

    try:
        yield db
    finally:
        db.close()
