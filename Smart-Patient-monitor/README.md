# Smart Patient Monitor Backend (Secure)

FastAPI backend for real-time biomedical patient monitoring with HTTPS TLS encryption and device API Key authentication.

---

## 1. Quick Setup

### Install Dependencies
```bash
cd backend
pip install -r ../requiremens.txt
```

### Configure Secret Key (.env)
Copy `.env.example` to `.env`:
```bash
cp .env.example .env
```
Ensure `ESP32_API_KEY` matches the secret key in your ESP32 firmware.

---

## 2. Generating Local SSL Certificates (HTTPS)

Generate local self-signed certificates with a single command:
```bash
python generate_cert.py
```
This generates `cert.pem` and `key.pem` inside `backend/`.

---

## 3. Running the Server

### With HTTPS (Secure Production / Dev):
```bash
uvicorn app.main:app --host 0.0.0.0 --port 8000 --ssl-keyfile key.pem --ssl-certfile cert.pem
```

### With Plain HTTP (Fallback / Dev):
```bash
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

---

## 4. API Security Details

- **Protected Endpoint:** `POST /api/v1/measurements`
- **Required Header:** `X-API-Key: <your_secret_key>`
- **Response on Invalid/Missing Key:** `401 Unauthorized` (with constant-time verification to prevent timing attacks).
- **Public Endpoints:**
  - `GET /`: Health & security configuration status
  - `GET /health`: Liveness probe
