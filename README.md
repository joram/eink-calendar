# eink-calendar

Firmware for a [Waveshare 7.5" e-Paper (B) V2](https://www.waveshare.com/7.5inch-e-paper-hat-b.htm) (800×480, black / white / red) driven by the [E-Paper ESP32 Driver Board](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board).

Code lives under **`firmware/`** (Arduino / PlatformIO).

## Build and flash

```bash
cd firmware
python3 -m venv .venv && .venv/bin/pip install platformio
```

From the repo root:

```bash
make build    # compile
make flash    # compile and upload (set `upload_port` in firmware/platformio.ini if needed)
```

Override the PlatformIO binary: `make flash PIO=pio`.

### WiFi and time

Copy `firmware/src/wifi_secrets.example.h` to `wifi_secrets.h` and set SSID, password, and `TZ_POSIX` (needed for correct local dates and API time windows).

### Google Calendar (e.g. john@oram.ca)

The firmware lists every calendar returned by the Calendar API (`calendarList`, reader access), then loads the next **three local days** of events from each.

1. In [Google Cloud Console](https://console.cloud.google.com/), enable **Google Calendar API** and create an **OAuth client ID** (Desktop app). Note the client ID and secret.
2. Add the redirect URI `http://127.0.0.1:8085/` (or `http://127.0.0.1:<port>/` if you use `--port`) under that client’s authorized redirect URIs.
3. From the repo root, run `make oauth-setup` (or `python3 tools/google_oauth_setup.py --client-id '…' --client-secret '…' -o firmware/src/google_secrets.h`). The tool prints the consent URL, opens a browser, listens on localhost, and writes `firmware/src/google_secrets.h` with the refresh token. To reuse saved id/secret, omit those flags if the header already exists.

Alternatively, copy `firmware/src/google_secrets.example.h` to `google_secrets.h` and paste credentials and a refresh token manually (e.g. from OAuth 2.0 Playground).

HTTPS uses the ESP32 TLS stack (`WiFiClientSecure`); for stricter validation you can switch from the current `setInsecure()` usage to a pinned CA bundle in code.
