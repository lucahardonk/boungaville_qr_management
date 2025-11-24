# 📘 QR Code Validation API

This service exposes a simple HTTP API to **validate QR codes** assigned to different access zones:

- **Corridoio**
- **Cancello**
- **Ingresso Boungaville**

Each zone maintains its own table and active status in the PostgreSQL database. The API allows external clients (e.g., QR code scanners or Arduino-based access systems) to verify if a given code is **active**, **inactive**, or **not found** in each specific zone.

---

## 📦 Base URL http://<your-server-ip>:5001

- Default port: `5001`
- Protocol: `HTTP`
- Replace `<your-server-ip>` with the actual IP address or hostname where this service is running.

---

## 🔐 Authentication

No authentication is required (in development).

> 🔒 **Note:** In production environments, consider adding token-based authentication, rate limiting, and HTTPS.

---

## 📥 API Endpoints

### ✅ `POST /validate_qr_corridoio`

Checks if a QR code exists and is active in the `qr_corridoio` table.


curl -X POST http://localhost:5001/validate_qr_corridoio \
     -H "Content-Type: application/json" \
     -d '{"code": "ABCDE"}'

---

### ✅ `POST /validate_qr_cancello`

Checks if a QR code exists and is active in the `qr_cancello` table.

---

### ✅ `POST /validate_qr_ingresso_boungaville`

Checks if a QR code exists and is active in the `qr_ingresso_boungaville` table.

---

## 📤 Request Format

- **Method:** `POST`
- **Header:** `Content-Type: application/json`
- **Body:**

```json
{
  "code": "ABCDE"
}


✅ Response Format

All endpoints return a JSON object with a status and a message.
HTTP Status	Condition	Example Response
200 OK	QR code exists and is active	{ "status": "success", "message": "QR code 'ABCDE' is valid and active" }
403	QR code exists but is inactive	{ "status": "inactive", "message": "QR code 'ABCDE' exists but is inactive" }
404	QR code does not exist	{ "status": "not_found", "message": "QR code 'ABCDE' does not exist" }
400	Missing or malformed input	{ "status": "error", "message": "QR code missing" }
500	Database/server error	{ "status": "error", "message": "Database error: <details>" }


the validation will be done every time and arduin is presented with a new scan, if the server returns sucess, it will open if not reachable it will chechk in it's memory, if so it will open, 

first time presented with qrcode, will check in memory, if veid will save it
if already known qr code is scanned and rejected by the server, it will remove from it's memory


password of the application
user: manager
pswd: qrcodes



moving application permission


sudo chown -R $USER:$USER postgres_data
mv boungaville_qr_management /some/other/location/
sudo chown -R 999:999 postgres_data

