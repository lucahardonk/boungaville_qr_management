from flask import Flask, request, jsonify
import psycopg2
import os
import logging
import threading
import requests
import time
from datetime import date

# 🧾 Configure logging
logging.basicConfig(level=logging.INFO, format='[%(asctime)s] %(levelname)s: %(message)s')

app = Flask(__name__)

DB_CONFIG = {
    "dbname": os.getenv("DB_NAME"),
    "user": os.getenv("DB_USER"),
    "password": os.getenv("DB_PASSWORD"),
    "host": os.getenv("DB_HOST"),
    "port": os.getenv("DB_PORT", 5432)
}

def validate_code_in_table(code, table_name):
    logging.info(f"📥 Validating code '{code}' in table '{table_name}'")
    try:
        conn = psycopg2.connect(**DB_CONFIG)
        cur = conn.cursor()
        cur.execute(f"SELECT active FROM {table_name} WHERE code = %s", (code,))
        result = cur.fetchone()
        cur.close()
        conn.close()

        if result is None:
            logging.info(f"❌ QR code '{code}' not found in table '{table_name}'")
            return {"status": "not_found", "message": f"QR code '{code}' does not exist"}, 404

        is_active = result[0]
        if is_active:
            logging.info(f"✅ QR code '{code}' is active in '{table_name}'")
            return {"status": "success", "message": f"QR code '{code}' is valid and active"}, 200
        else:
            logging.info(f"⚠️ QR code '{code}' is found but inactive in '{table_name}'")
            return {"status": "inactive", "message": f"QR code '{code}' exists but is inactive"}, 403

    except Exception as e:
        logging.error(f"❌ Database error while checking code '{code}' in '{table_name}': {e}")
        return {"status": "error", "message": f"Database error: {e}"}, 500

@app.route('/validate_qr_corridoio', methods=['POST'])
def validate_qr_corridoio():
    data = request.json
    code = data.get('code', '').strip().upper()
    if not code:
        return jsonify({"status": "error", "message": "QR code missing"}), 400
    return jsonify(*validate_code_in_table(code, "qr_corridoio"))

@app.route('/validate_qr_cancello', methods=['POST'])
def validate_qr_cancello():
    data = request.json
    code = data.get('code', '').strip().upper()
    if not code:
        return jsonify({"status": "error", "message": "QR code missing"}), 400
    return jsonify(*validate_code_in_table(code, "qr_cancello"))

@app.route('/validate_qr_ingresso_boungaville', methods=['POST'])
def validate_qr_ingresso():
    data = request.json
    code = data.get('code', '').strip().upper()
    if not code:
        return jsonify({"status": "error", "message": "QR code missing"}), 400
    return jsonify(*validate_code_in_table(code, "qr_ingresso_boungaville"))

# 🔁 Background thread to manage eliminate old QR codes
def check_qrcodes_loop():
    while True:
        try:
            conn = psycopg2.connect(**DB_CONFIG)
            cur = conn.cursor()
            cur.execute("SELECT code, date_in, date_out FROM qrcode")
            all_codes = cur.fetchall()

            today = date.today()

            for code, date_in, date_out in all_codes:
                code = code.strip().upper()

                # If both dates are in the past, delete the QR code
                if date_out < today:
                    try:
                        cur.execute("DELETE FROM qrcode WHERE code = %s", (code,))
                        logging.info(f"🗑️ Deleted expired QR code: {code}")
                    except Exception as e:
                        logging.error(f"❌ Error deleting expired QR code {code}: {e}")

            conn.commit()
            cur.close()
            conn.close()

        except Exception as e:
            logging.error(f"❌ Global error in QR code loop: {e}")

        time.sleep(30)

# 🧵 Start background thread
threading.Thread(target=check_qrcodes_loop, daemon=True).start()



if __name__ == '__main__':
    logging.info("🚀 Starting QR code validator server on port 5001...")
    app.run(debug=True, host="0.0.0.0", port=5001)
