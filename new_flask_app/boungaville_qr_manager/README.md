# Boungaville QR Manager

A small Flask web application for creating and managing time-limited access QR codes
across a set of **QR Code Reader** devices (door / gate controllers). From a single
dashboard you can:

- Create a QR code for a guest (name, surname, check-in and check-out date/time) and
  push it to every enabled reader at once.
- List all QR codes currently stored on the readers, with their status
  (`pending`, `active`, `expired`).
- Delete a QR code from a specific device.
- Monitor the online/offline status of each reader.

Authentication is handled with a **custom HTML login page** — the app shows a styled login
form where the username is pre-filled and you only need to enter your password. A simple
in-memory authentication flag keeps you logged in for 5 minutes (auto-resets after timeout).
Credentials are read from Home Assistant options (when running as an add-on) or from a local
`.env` file (local development) and never hardcoded in the source.

## QR Code Reader devices

The dashboard talks to the following readers over their local HTTP API. Their addresses
are configured in `qr_code_manager.py` (the `DEVICES` list):

| Device                                   | Address              |
| ---------------------------------------- | -------------------- |
| **QRCode Corridoio**                     | `http://192.168.1.65` |
| **QRCode Cancello**                      | `http://192.168.1.70` |
| **QRCode porta Ingresso Bougainville**   | `http://192.168.1.73` |

> These are private LAN addresses, so the app must run on the same network as the readers.

## Setup

1. **Clone the repository**

   ```bash
   git clone https://github.com/lucahardonk/boungaville_qr_management.git
   cd boungaville_qr_management/new_flask_app/boungaville_qr_manager
   ```

2. **Install dependencies**

   ```bash
   pip install -r requirements.txt
   ```

3. **Configure credentials**

   Copy the example environment file and set your own password (and, optionally, a
   different username):

   ```bash
   cp .env.example .env
   ```

   Then edit `.env`:

   ```
   ADMIN_USERNAME=admin
   ADMIN_PASSWORD=your-strong-password-here
   ```

   `ADMIN_PASSWORD` is required — the app will refuse to start if it is not set.
   `ADMIN_USERNAME` is optional and defaults to `admin`.

4. **Run the app**

   ```bash
   python qr_code_manager.py
   ```

5. **Open the dashboard**

   Visit [http://localhost:8086](http://localhost:8086). You will see the **QR Code Manager
   login page** — the username is pre-filled as `admin`. Enter the password you set in `.env`
   and click **Login**.

## Running with Docker

### Build the image

> **Always `git pull` before rebuilding** to make sure Docker uses the latest code:
> ```bash
> git pull origin feature/simplify-auth-dotenv   # or main, once the PR is merged
> ```

```bash
cd new_flask_app/boungaville_qr_manager
docker build -t boungaville-qr-manager .
```

### Run the container
Pass credentials via `--env-file` (recommended — `.env` is never baked into the image):
```bash
docker run --rm -p 8086:8086 --env-file .env boungaville-qr-manager
```

Or pass them directly with `-e` flags:
```bash
docker run --rm -p 8086:8086 \
  -e ADMIN_USERNAME=admin \
  -e ADMIN_PASSWORD=your-strong-password \
  boungaville-qr-manager
```

Then open `http://localhost:8086` in your browser. The **QR Code Manager login page** will appear — the username is pre-filled, just enter your password.

### Testing locally without Docker
```bash
cd new_flask_app/boungaville_qr_manager
pip install -r requirements.txt
cp .env.example .env        # then edit .env and set your password
python qr_code_manager.py
```
Open `http://localhost:8086` — you will see the login page. Enter your credentials and click **Login**.

> **Note:** The QR reader devices (192.168.1.65, 192.168.1.70, 192.168.1.73) must be reachable from the machine running the container. If you are running Docker on a different network segment, ensure routing/VPN is configured accordingly.

## Deploying to Home Assistant as a Local Add-on

This application can be installed as a **local add-on** in Home Assistant. Follow these steps to deploy or update the add-on:

### Initial Installation

1. **SSH into your Home Assistant instance** (enable SSH add-on first if not already done).

2. **Clone the repository into `/tmp`:**
   ```bash
   cd /tmp
   git clone -b feature/simplify-auth-dotenv https://github.com/lucahardonk/boungaville_qr_management.git
   ```

3. **Copy the add-on files to the local add-ons directory:**
   ```bash
   cp -a /tmp/boungaville_qr_management/new_flask_app/boungaville_qr_manager \
         /root/addons/local/boungaville_qr_manager
   ```
   
   > **Note:** Depending on your HA installation, the path might be `/addons/local` instead of `/root/addons/local`. Adjust accordingly.

4. **Clean up unwanted files:**
   ```bash
   rm -rf /root/addons/local/boungaville_qr_manager/venv
   rm -rf /root/addons/local/boungaville_qr_manager/__pycache__
   rm -f /root/addons/local/boungaville_qr_manager/.env
   ```

5. **Fix line endings and permissions on the startup script:**
   ```bash
   sed -i 's/\r$//' /root/addons/local/boungaville_qr_manager/run.sh
   chmod +x /root/addons/local/boungaville_qr_manager/run.sh
   ```

6. **Reload the add-on store** in Home Assistant:
   - Go to **Settings** → **Add-ons** → **Add-on Store** (three dots menu) → **Reload**
   - The add-on should now appear in the local add-ons section

7. **Install and configure:**
   - Click on **Boungaville QR Management** → **Install**
   - Go to the **Configuration** tab and set your `admin_username` and `admin_password`
   - Click **Save**, then **Start**

### Updating the Add-on (Important!)

When you push changes to the GitHub repository and want to update the running add-on in Home Assistant, follow these steps:

> **CRITICAL:** Simply copying new files is NOT enough — Home Assistant caches the Docker image. You MUST force a rebuild using the `ha addons update` (or `ha apps update`) command, otherwise the changes will not take effect.

1. **SSH into Home Assistant**

2. **Pull the latest changes from GitHub:**
   ```bash
   cd /tmp
   rm -rf boungaville_qr_management
   git clone -b feature/simplify-auth-dotenv https://github.com/lucahardonk/boungaville_qr_management.git
   ```

3. **Replace the local add-on files:**
   ```bash
   rm -rf /root/addons/local/boungaville_qr_manager
   cp -a /tmp/boungaville_qr_management/new_flask_app/boungaville_qr_manager \
         /root/addons/local/boungaville_qr_manager
   
   rm -rf /root/addons/local/boungaville_qr_manager/venv
   rm -rf /root/addons/local/boungaville_qr_manager/__pycache__
   rm -f /root/addons/local/boungaville_qr_manager/.env
   
   sed -i 's/\r$//' /root/addons/local/boungaville_qr_manager/run.sh
   chmod +x /root/addons/local/boungaville_qr_manager/run.sh
   ```

4. **Force the add-on to rebuild and update:**
   ```bash
   ha apps update local_boungaville_qr_manager
   ```
   
   > **Note:** On older HA versions, use `ha addons update local_boungaville_qr_manager`
   
   This command forces Home Assistant to rebuild the Docker image from the updated source files. Without this step, the add-on will continue running the old cached image.

5. **Restart the add-on** from the Home Assistant UI or via CLI:
   ```bash
   ha apps restart local_boungaville_qr_manager
   ```

6. **Verify the version:**
   ```bash
   ha apps info local_boungaville_qr_manager | grep version
   ```
   
   You should see the updated version number from `config.json`.

### Home Assistant Configuration

When running as a Home Assistant add-on, credentials are configured through the add-on's **Configuration** tab:

- `admin_username`: The username for logging in (default: `admin`)
- `admin_password`: Your secure password (**required**)

These values are passed to the container via Home Assistant's options system (`/data/options.json` inside the container). The `.env` file is only used for local development.

## Configuration

The list of QR Code Reader devices lives in the `DEVICES` list near the top of
`qr_code_manager.py`. Each entry has three fields:

```python
DEVICES = [
    {
        'name': 'QRCode Corridoio',   # label shown in the dashboard
        'url': 'http://192.168.1.65', # base URL of the reader's HTTP API
        'enabled': True               # whether the app talks to this device
    },
    ...
]
```

- **Add a device** — append a new dictionary with its `name`, `url` and `enabled: True`.
- **Remove a device** — delete its dictionary from the list.
- **Temporarily disable a device** — set `'enabled': False`. Disabled devices are skipped
  when creating codes, listing codes and checking status, without being removed from the
  configuration.

Restart the app after changing `DEVICES`.

## Security notes

- **Never commit your `.env` file.** It contains your real password. `.env` is already
  listed in `.gitignore`; only `.env.example` (with placeholder values) is tracked.
- **Change the default password.** Do not ship `your-strong-password-here` — set a strong,
  unique `ADMIN_PASSWORD`.
- **Authentication timeout:** The in-memory login flag expires after 5 minutes of inactivity.
  This is a simple stateless approach designed to work reliably behind Home Assistant Ingress
  (which can strip cookies). For local development on untrusted networks, put the app behind
  HTTPS (e.g. a reverse proxy with TLS) to prevent credentials from being sent in clear text.
