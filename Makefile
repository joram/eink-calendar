FIRMWARE_DIR := firmware
# Resolved after cd $(FIRMWARE_DIR). Override: make flash PIO=pio
PIO ?= .venv/bin/pio

.PHONY: build flash build-esp32 flash-esp32 oauth-setup

# PlatformIO: needs firmware/.venv with PlatformIO; serial: dialout group or chmod /dev/ttyACM*
build:
	cd $(FIRMWARE_DIR) && $(PIO) run

flash: build
	cd $(FIRMWARE_DIR) && $(PIO) run -t upload --upload-port /dev/ttyACM0

build-esp32: build

flash-esp32: flash


logs:
	cd $(FIRMWARE_DIR) && $(PIO) device monitor -b 115200

# OAuth: add http://127.0.0.1:8085/ (or your --port) to the Desktop OAuth client redirect URIs
oauth-setup:
	python3 tools/google_oauth_setup.py -o firmware/src/google_secrets.h