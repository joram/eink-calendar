# eink-calendar

Displays a calendar on a [Waveshare 7.5" e-Paper HAT (B) V3](https://www.waveshare.com/7.5inch-e-paper-hat-b.htm) connected to a Raspberry Pi. The display refreshes once an hour and runs as a systemd service on boot.

## Hardware

- Raspberry Pi (any model with 40-pin GPIO)
- Waveshare 7.5" e-Paper HAT (B) V3 — 800×480, black/white/red

## Pi setup (first time)

### 1. Flash and configure the Pi

Flash Raspberry Pi OS Lite and during first boot (or via `raspi-config`):

- Set hostname to `calendar`
- Set username to `john`
- Enable SSH
- Connect to your network

### 2. Enable SPI

SSH into the Pi and run:

```bash
sudo raspi-config
```

Navigate to **Interface Options → SPI → Enable**, then reboot.

### 3. Install git

```bash
sudo apt update && sudo apt install -y git
```

### 4. Deploy from your dev machine

Back on your dev machine, from this repo:

```bash
make install
```

This will:
- Clone the repo to `~/eink-calendar` on the Pi
- Install Python dependencies (`pillow`, `RPi.GPIO`, `spidev`)
- Clone the Waveshare e-Paper driver library
- Install and enable the `eink-calendar` systemd service

The display will start updating immediately and will restart automatically on boot.

## Ongoing deployment

After pushing changes to `main`, deploy to the Pi with:

```bash
make update
```

This SSHes into the Pi, pulls the latest code, and restarts the service.

## Checking status on the Pi

```bash
# View service status
sudo systemctl status eink-calendar

# Follow live logs
journalctl -u eink-calendar -f
```

## Wiring

The HAT connects directly to the Pi's 40-pin GPIO header — no wiring needed if using the HAT form factor.
