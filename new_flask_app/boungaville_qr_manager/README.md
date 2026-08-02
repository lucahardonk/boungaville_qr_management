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

Authentication is handled with **stateless HTTP Basic Auth** — the browser prompts for a
username and password on the first request. There are no sessions or cookies, and the
credentials are read from a local `.env` file.

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

   Visit [http://localhost:8086](http://localhost:8086). Your browser will show a native
   login prompt (HTTP Basic Auth) — enter the username and password from your `.env`
   file.

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
- HTTP Basic Auth sends credentials on every request. On any network you don't fully
  control, put the app behind HTTPS (e.g. a reverse proxy with TLS) so the credentials are
  never sent in clear text.
