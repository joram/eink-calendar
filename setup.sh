#!/bin/bash
# Setup Waveshare e-Paper library on Raspberry Pi

set -e

# Enable SPI if not already enabled
if ! grep -q "^dtparam=spi=on" /boot/config.txt 2>/dev/null && \
   ! grep -q "^dtparam=spi=on" /boot/firmware/config.txt 2>/dev/null; then
    echo "SPI not enabled. Run: sudo raspi-config -> Interface Options -> SPI -> Enable"
    echo "Or add 'dtparam=spi=on' to /boot/firmware/config.txt and reboot."
fi

# Install dependencies
sudo apt install -y python3-pil python3-rpi.gpio python3-spidev python3-numpy python3-googleapi python3-google-auth-oauthlib

# Clone Waveshare library
if [ ! -d "e-Paper" ]; then
    git clone https://github.com/waveshare/e-Paper.git
fi

echo "Setup complete."
echo "Run: python3 draw_circle.py"
