# ─────────────────────────────────────────────────────────────────
# Boungaville QR Manager — Docker image
# Credentials are NOT baked into the image.
# Pass them at runtime:
#   docker run --env-file .env ...
# or individually:
#   docker run -e ADMIN_USERNAME=admin -e ADMIN_PASSWORD=secret ...
# ─────────────────────────────────────────────────────────────────
FROM python:3.11-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

RUN chmod +x /app/run.sh

CMD ["/bin/bash", "/app/run.sh"]
