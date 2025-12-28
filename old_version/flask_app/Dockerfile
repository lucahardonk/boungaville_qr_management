FROM python:3.10-slim

# Set working directory
WORKDIR /app

# Install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Set Italian locale
RUN apt-get update && apt-get install -y --no-install-recommends locales && \
    sed -i '/it_IT.UTF-8/s/^# //g' /etc/locale.gen && \
    locale-gen && \
    update-locale LANG=it_IT.UTF-8

ENV LANG=it_IT.UTF-8
ENV LANGUAGE=it_IT:it
ENV LC_ALL=it_IT.UTF-8

# Copy the app
COPY . .

RUN chmod +x start_script.sh

# Run both Flask services
CMD ["/bin/bash", "start_script.sh"]

