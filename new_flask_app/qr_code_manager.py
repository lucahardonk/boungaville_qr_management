"""
QR Code Creator & Manager
Flask web application for managing QR codes across Restaurant Manager devices
Version: 1.4 - Added date validation
Date: 2025-12-28
"""

from flask import Flask, render_template, request, jsonify, session, redirect, url_for
import requests
from datetime import datetime
import json
import qrcode
import io
import base64
from functools import wraps
import re

app = Flask(__name__)
app.secret_key = 'change-this-secret-key-in-production'  # ⚠️ CHANGE THIS!

# Configuration
DEVICES = [
    {
        'name': 'Restaurant Manager',
        'url': 'http://192.168.1.97',
        'enabled': True
    }
]

DEFAULT_USERNAME = 'admin'
DEFAULT_PASSWORD = 'admin123'  # ⚠️ CHANGE THIS!

# Authentication decorator
def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'logged_in' not in session:
            return redirect(url_for('login'))
        return f(*args, **kwargs)
    return decorated_function

@app.route('/')
def index():
    if 'logged_in' in session:
        return redirect(url_for('dashboard'))
    return redirect(url_for('login'))

@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get('username', '')
        password = request.form.get('password', '')
        
        if username == DEFAULT_USERNAME and password == DEFAULT_PASSWORD:
            session['logged_in'] = True
            session['username'] = username
            return redirect(url_for('dashboard'))
        else:
            return render_template('login.html', error='Invalid credentials', default_username=DEFAULT_USERNAME)
    
    return render_template('login.html', default_username=DEFAULT_USERNAME)

@app.route('/logout')
def logout():
    session.clear()
    return redirect(url_for('login'))

@app.route('/dashboard')
@login_required
def dashboard():
    return render_template('dashboard.html', devices=DEVICES, username=session.get('username'))

@app.route('/api/create_qr', methods=['POST'])
@login_required
def create_qr():
    try:
        data = request.json
        name = data.get('name', '').strip()
        surname = data.get('surname', '').strip()
        date_in = data.get('date_in', '').strip()
        date_out = data.get('date_out', '').strip()
        
        # Validation
        if not name or not surname:
            return jsonify({'success': False, 'message': 'Name and surname are required'}), 400
        
        if len(name) > 30:
            return jsonify({'success': False, 'message': 'Name too long (max 30 chars)'}), 400
        
        if len(surname) > 30:
            return jsonify({'success': False, 'message': 'Surname too long (max 30 chars)'}), 400
        
        # Validate date format
        date_pattern = r'^\d{4}-\d{2}-\d{2}-\d{2}-\d{2}-\d{2}$'
        if not re.match(date_pattern, date_in) or not re.match(date_pattern, date_out):
            return jsonify({'success': False, 'message': 'Invalid date format. Use YYYY-MM-DD-HH-MM-SS'}), 400
        
        # Validate dates can be parsed and check-out is after check-in
        try:
            date_in_dt = datetime.strptime(date_in, '%Y-%m-%d-%H-%M-%S')
            date_out_dt = datetime.strptime(date_out, '%Y-%m-%d-%H-%M-%S')
            
            # Check that check-out is strictly after check-in
            if date_out_dt <= date_in_dt:
                return jsonify({'success': False, 'message': 'Check-out time must be later than check-in time'}), 400
            
        except ValueError:
            return jsonify({'success': False, 'message': 'Invalid date values'}), 400
        
        # Create a simple pipe-delimited string instead of JSON
        # Format: name|surname|date_in|date_out
        qr_data_string = f"{name}|{surname}|{date_in}|{date_out}"
        
        # Check if value length is within limits (128 chars)
        if len(qr_data_string) > 128:
            return jsonify({'success': False, 'message': f'Data too long ({len(qr_data_string)} chars, max 128)'}), 400
        
        # Send to all enabled devices
        results = []
        success_count = 0
        
        for device in DEVICES:
            if not device['enabled']:
                continue
            
            try:
                # Create the payload - value is now a simple string
                payload = {
                    'value': qr_data_string
                }
                
                print(f"Sending to {device['name']}: {json.dumps(payload)}")
                
                response = requests.post(
                    f"{device['url']}/api/insert",
                    json=payload,
                    timeout=5
                )
                
                print(f"Response status: {response.status_code}")
                print(f"Response body: {response.text}")
                
                if response.status_code == 200:
                    result_data = response.json()
                    if result_data.get('success'):
                        success_count += 1
                        results.append({
                            'device': device['name'],
                            'success': True,
                            'key': result_data.get('key', 'unknown')
                        })
                    else:
                        results.append({
                            'device': device['name'],
                            'success': False,
                            'error': result_data.get('message', 'Unknown error')
                        })
                else:
                    results.append({
                        'device': device['name'],
                        'success': False,
                        'error': f'HTTP {response.status_code}: {response.text}'
                    })
            except requests.exceptions.RequestException as e:
                results.append({
                    'device': device['name'],
                    'success': False,
                    'error': str(e)
                })
        
        # Generate QR code image
        qr = qrcode.QRCode(version=1, box_size=10, border=4)
        qr.add_data(qr_data_string)
        qr.make(fit=True)
        img = qr.make_image(fill_color="black", back_color="white")
        
        # Convert to base64
        buffer = io.BytesIO()
        img.save(buffer, format='PNG')
        img_str = base64.b64encode(buffer.getvalue()).decode()
        
        return jsonify({
            'success': success_count > 0,
            'message': f'Sent to {success_count}/{len([d for d in DEVICES if d["enabled"]])} devices',
            'results': results,
            'qr_image': img_str,
            'qr_data': qr_data_string
        })
        
    except Exception as e:
        print(f"Error in create_qr: {str(e)}")
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'message': str(e)}), 500

@app.route('/api/get_all_qr_codes')
@login_required
def get_all_qr_codes():
    try:
        all_codes = []
        
        for device in DEVICES:
            if not device['enabled']:
                continue
            
            try:
                print(f"Fetching QR codes from {device['name']} at {device['url']}/api/print")
                response = requests.get(
                    f"{device['url']}/api/print",
                    timeout=5
                )
                
                print(f"Response status: {response.status_code}")
                print(f"Response body: {response.text}")
                
                if response.status_code == 200:
                    data = response.json()
                    if data.get('success'):
                        for item in data.get('data', []):
                            try:
                                # Parse the pipe-delimited string
                                qr_value_string = item['value']
                                print(f"Parsing QR value: {qr_value_string}")
                                
                                # Split by pipe: name|surname|date_in|date_out
                                parts = qr_value_string.split('|')
                                if len(parts) == 4:
                                    name, surname, date_in, date_out = parts
                                    
                                    # Calculate status
                                    now = datetime.now()
                                    date_in_dt = datetime.strptime(date_in, '%Y-%m-%d-%H-%M-%S')
                                    date_out_dt = datetime.strptime(date_out, '%Y-%m-%d-%H-%M-%S')
                                    
                                    if now < date_in_dt:
                                        status = 'pending'
                                    elif date_in_dt <= now <= date_out_dt:
                                        status = 'active'
                                    else:
                                        status = 'expired'
                                    
                                    all_codes.append({
                                        'device': device['name'],
                                        'key': item['key'],
                                        'name': name,
                                        'surname': surname,
                                        'date_in': date_in,
                                        'date_out': date_out,
                                        'status': status,
                                        'raw_value': qr_value_string
                                    })
                                else:
                                    print(f"Invalid format: expected 4 parts, got {len(parts)}")
                            except (ValueError, IndexError) as e:
                                print(f"Skipping invalid entry: {item.get('value', 'N/A')} - Error: {e}")
                                continue
                    else:
                        print(f"Device {device['name']} returned success=false: {data.get('message', 'No message')}")
                else:
                    print(f"Device {device['name']} returned HTTP {response.status_code}: {response.text}")
            except requests.exceptions.RequestException as e:
                print(f"Error connecting to {device['name']}: {e}")
                continue
        
        return jsonify({
            'success': True,
            'codes': all_codes,
            'count': len(all_codes)
        })
        
    except Exception as e:
        print(f"Error in get_all_qr_codes: {str(e)}")
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'message': str(e)}), 500

@app.route('/api/delete_qr', methods=['POST'])
@login_required
def delete_qr():
    try:
        data = request.json
        device_name = data.get('device')
        qr_value = data.get('value')
        
        # Find device
        device = next((d for d in DEVICES if d['name'] == device_name), None)
        if not device:
            return jsonify({'success': False, 'message': 'Device not found'}), 404
        
        # Send delete request
        payload = {'value': qr_value}
        print(f"Deleting from {device['name']}: {json.dumps(payload)}")
        
        response = requests.post(
            f"{device['url']}/api/remove",
            json=payload,
            timeout=5
        )
        
        print(f"Delete response: {response.status_code} - {response.text}")
        
        if response.status_code == 200:
            result = response.json()
            return jsonify(result)
        else:
            return jsonify({'success': False, 'message': f'HTTP {response.status_code}'}), response.status_code
            
    except Exception as e:
        print(f"Error in delete_qr: {str(e)}")
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'message': str(e)}), 500

@app.route('/api/check_device_status')
@login_required
def check_device_status():
    """Check status of all enabled devices"""
    try:
        device_statuses = []
        
        for device in DEVICES:
            if not device['enabled']:
                continue
            
            try:
                # Try to ping the device with a quick timeout
                response = requests.get(
                    f"{device['url']}/api/time",  # Use the time endpoint as a ping
                    timeout=1  # 1 second timeout
                )
                
                device_statuses.append({
                    'name': device['name'],
                    'online': response.status_code == 200
                })
            except:
                device_statuses.append({
                    'name': device['name'],
                    'online': False
                })
        
        return jsonify({
            'success': True,
            'devices': device_statuses
        })
        
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)})
if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)