#!/usr/bin/env bash
set -e

if [ -f /data/options.json ]; then
    export ADMIN_USERNAME="$(python3 -c "import json; print(json.load(open('/data/options.json')).get('admin_username', 'admin'))")"
    export ADMIN_PASSWORD="$(python3 -c "import json; print(json.load(open('/data/options.json')).get('admin_password', ''))")"
fi

if [ -z "${ADMIN_PASSWORD:-}" ]; then
    echo "ERROR: admin_password has not been configured."
    echo "Set it in the Home Assistant add-on Configuration tab."
    exit 1
fi

exec python3 /app/qr_code_manager.py