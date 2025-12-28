import logging
import os
import random
import string
import psycopg2
import requests
import subprocess
import platform
from datetime import datetime
from flask import Flask, render_template, request, redirect, url_for, session
from werkzeug.security import check_password_hash, generate_password_hash

app = Flask(__name__)
app.secret_key = os.getenv("FLASK_SECRET_KEY", "dev-secret-key")

# 🔧 Logging config
logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s'
)

# 🔌 DB config
DB_CONFIG = {
    "dbname": os.getenv("DB_NAME"),
    "user": os.getenv("DB_USER"),
    "password": os.getenv("DB_PASSWORD"),
    "host": os.getenv("DB_HOST"),
    "port": os.getenv("DB_PORT", 5432)
}

# 🔐 Login page
@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form['username'].strip().lower()
        password = request.form['password']

        try:
            conn = psycopg2.connect(**DB_CONFIG)
            cur = conn.cursor()
            cur.execute("SELECT password_hash FROM users WHERE username = %s", (username,))
            result = cur.fetchone()
            cur.close()
            conn.close()

            if result:
                db_hash = result[0]
                is_correct = check_password_hash(db_hash, password)

                logging.info(f"🔐 Stored hash for '{username}': {db_hash}")
                logging.info(f"🔍 Password match: {'✅' if is_correct else '❌'}")

                if is_correct and username == 'manager':
                    session['username'] = username
                    logging.info(f"✅ Manager login successful: {username}")
                    return redirect(url_for('submit_info'))
                elif is_correct:
                    return render_template('login.html', message="❌ Access denied. Only 'manager' is allowed.")
                else:
                    return render_template('login.html', message="❌ Invalid password")

            else:
                logging.warning("❌ User not found")
                return render_template('login.html', message="❌ Invalid username or password")

        except Exception as e:
            logging.error(f"❌ Login Error: {e}")
            return render_template('login.html', message=f"❌ Login error: {e}")

    return render_template('login.html')




@app.route('/', methods=['GET', 'POST'])
def submit_info():
    if 'username' not in session:
        logging.warning("🔒 Unauthorized access to form page")
        return redirect(url_for('login'))

    if request.method == 'POST':
        name = request.form['name']
        surname = request.form['surname']
        dateIn = request.form['dateIn']
        dateOut = request.form['dateOut']

        # ✅ Convert string to datetime.date for comparison
        try:
            date_in_obj = datetime.strptime(dateIn, "%Y-%m-%d").date()
            date_out_obj = datetime.strptime(dateOut, "%Y-%m-%d").date()
        except ValueError:
            return render_template('form.html', submitted=False, message="❌ Invalid date format.")

        if date_in_obj > date_out_obj:
            return render_template('form.html', submitted=False, message="⚠️ Date In cannot be after Date Out.")

        try:
            conn = psycopg2.connect(**DB_CONFIG)
            cur = conn.cursor()

            # 🔍 Check for existing record with same name/surname/dates
            cur.execute("""
                SELECT * FROM qrcode
                WHERE name = %s AND surname = %s AND date_in = %s AND date_out = %s
            """, (name, surname, dateIn, dateOut))

            existing = cur.fetchone()

            if existing:
                logging.warning("⚠️ QR code already exists for this data")
                cur.close()
                conn.close()
                return render_template('form.html', submitted=False, message="⚠️ QR code already exists for that user and date range.")

            # ✅ All fields are unique — insert
            def generate_unique_code(cursor):
                while True:
                    code = ''.join(random.choices(string.ascii_uppercase + string.digits, k=5))
                    cursor.execute("SELECT 1 FROM qrcode WHERE code = %s", (code,))
                    if not cursor.fetchone():
                        return code

            code = generate_unique_code(cur)


            # Notify ingresso
            arduino1_ok = ping_device("192.168.1.157")  # Replace with actual IP
            if not arduino1_ok:
                logging.warning("⚠️ Ingresso Arduino is unreachable.")
            else:
                logging.info("✅ Ingresso Arduino is online.")

            # Notify corridoio
            arduino2_ok = ping_device("192.168.1.157")  # Replace with actual IP
            if not arduino2_ok:
                logging.warning("⚠️ Corridoio Arduino is unreachable.")
            else:
                logging.info("✅ Corridoio Arduino is online.")

            # Notify cancello
            arduino3_ok = ping_device("192.168.1.157")  # Replace with actual IP
            if not arduino3_ok:
                logging.warning("⚠️ Cancello Arduino is unreachable.")
            else:
                logging.info("✅ Cancello Arduino is online.")



            cur.execute("""
                INSERT INTO qrcode (name, surname, date_in, date_out, code)
                VALUES (%s, %s, %s, %s, %s)
            """, (name, surname, dateIn, dateOut, code))

            cur.execute("INSERT INTO qr_cancello (code, active) VALUES (%s, TRUE)", (code,))
            cur.execute("INSERT INTO qr_corridoio (code, active) VALUES (%s, TRUE)", (code,))
            cur.execute("INSERT INTO qr_ingresso_boungaville (code, active) VALUES (%s, TRUE)", (code,))

            conn.commit()
            cur.close()
            conn.close()

            logging.info(f"✅ Inserted new QR entry for {name} {surname}, code: {code}")

            return render_template(
                'form.html',
                submitted=True,
                name=name,
                surname=surname,
                dateIn=dateIn,
                dateOut=dateOut,
                code=code,
                arduino1_ok=arduino1_ok,
                arduino2_ok=arduino2_ok,
                arduino3_ok=arduino3_ok,
                message="✅ Entry added successfully!"
            )



        except Exception as e:
            logging.error(f"❌ DB error: {e}")
            return f"Error: {e}"

    return render_template(
        'form.html',
        submitted=False,
        arduino1_ok=False,
        arduino2_ok=False,
        arduino3_ok=False
    )




# manage qr code page

@app.route('/manage')
def manage_qrcodes():
    if 'username' not in session:
        return redirect(url_for('login'))

    try:
        conn = psycopg2.connect(**DB_CONFIG)
        cur = conn.cursor()

        # Retrieve all QR codes and their active status from all 3 zone tables
        cur.execute("""
            SELECT q.code, q.name, q.surname, q.date_in, q.date_out,
                c.active AS corridoio_active,
                ca.active AS cancello_active,
                i.active AS ingresso_active
            FROM qrcode q
            LEFT JOIN qr_corridoio c ON q.code = c.code
            LEFT JOIN qr_cancello ca ON q.code = ca.code
            LEFT JOIN qr_ingresso_boungaville i ON q.code = i.code
            ORDER BY q.id DESC;
        """)
        qrcodes = cur.fetchall()
        cur.close()
        conn.close()

        return render_template('manage.html', qrcodes=qrcodes)

    except Exception as e:
        logging.error(f"❌ Failed to load QR management page: {e}")
        return f"Error: {e}"

@app.route('/toggle_qr/<zone>/<code>', methods=['POST'])
def toggle_qr(zone, code):
    if 'username' not in session:
        return redirect(url_for('login'))

    valid_tables = {
        'corridoio': 'qr_corridoio',
        'cancello': 'qr_cancello',
        'boungaville': 'qr_ingresso_boungaville'
    }

    if zone not in valid_tables:
        return "❌ Invalid zone"

    table = valid_tables[zone]

    try:
        conn = psycopg2.connect(**DB_CONFIG)
        cur = conn.cursor()

        # Toggle the current active value
        cur.execute(
            f"UPDATE {table} SET active = NOT active WHERE code = %s",
            (code,)
        )
        conn.commit()
        cur.close()
        conn.close()

        logging.info(f"🔁 Toggled active state for {code} in {table}")
        return redirect(url_for('manage_qrcodes'))

    except Exception as e:
        logging.error(f"❌ Toggle error: {e}")
        return f"Toggle error: {e}"


@app.route('/delete_qr/<code>', methods=['POST'])
def delete_qr(code):
    if 'username' not in session:
        return redirect(url_for('login'))

    try:
        conn = psycopg2.connect(**DB_CONFIG)
        cur = conn.cursor()

        # 🔥 Delete from qrcode — ON DELETE CASCADE will clear zone tables
        cur.execute("DELETE FROM qrcode WHERE code = %s", (code,))

        conn.commit()
        cur.close()
        conn.close()

        logging.info(f"🗑️ Deleted QR code: {code}")
        return redirect(url_for('manage_qrcodes'))

    except Exception as e:
        logging.error(f"❌ Delete error for code {code}: {e}")
        return f"Delete error: {e}"


# 🚪 Logout route
@app.route('/logout')
def logout():
    logging.info(f"🔓 User {session.get('username')} logged out")
    session.pop('username', None)
    return redirect(url_for('login'))




### function ping ###
def ping_device(ip: str, timeout: int = 2) -> bool:
    """
    Ping a device by IP on Linux. Returns True if reachable, False otherwise.
    """
    try:
        result = subprocess.run(
            ["ping", "-c", "1", "-W", str(timeout), ip],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        return result.returncode == 0
    except Exception as e:
        logging.error(f"❌ Ping to {ip} failed: {e}")
        return False



if __name__ == '__main__':
    
  app.run(debug=True, host='0.0.0.0')


