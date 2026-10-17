
from fastapi import FastAPI

app = FastAPI(
    title="Smart Patient Monitor API",
    version="0.1.0",
)


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
