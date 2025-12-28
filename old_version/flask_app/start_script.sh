#!/bin/bash

# Run app.py in the background
python /app/app.py &

# Run verify_in_db_qrcode.py in the foreground
python /app/verify_in_db_qrcode.py
