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
pip3 install pillow RPi.GPIO spidev

# Clone Waveshare library
if [ ! -d "e-Paper" ]; then
    git clone https://github.com/waveshare/e-Paper.git
fi

echo "Setup complete."
echo "Run: python3 draw_circle.py"
