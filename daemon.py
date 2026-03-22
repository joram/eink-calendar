import sys
import os
import time
import logging

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'e-Paper/RaspberryPi_JetsonNano/python/lib'))

from waveshare_epd import epd7in5b_V3
from PIL import Image, ImageDraw

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s %(levelname)s %(message)s',
)
log = logging.getLogger(__name__)

REFRESH_INTERVAL = 3600  # seconds


def render(epd):
    img_black = Image.new('1', (epd.width, epd.height), 255)
    img_red   = Image.new('1', (epd.width, epd.height), 255)

    draw_black = ImageDraw.Draw(img_black)
    draw_red   = ImageDraw.Draw(img_red)

    # TODO: draw calendar content here
    cx, cy, r = epd.width // 2, epd.height // 2, 100
    draw_black.ellipse((cx - r, cy - r, cx + r, cy + r), outline=0, width=4)

    r2 = 40
    draw_red.ellipse((cx + 80 - r2, cy - r2, cx + 80 + r2, cy + r2), fill=0)

    epd.display(epd.getbuffer(img_black), epd.getbuffer(img_red))


def main():
    log.info("Starting eink-calendar daemon")
    epd = epd7in5b_V3.EPD()
    epd.init()

    while True:
        log.info("Refreshing display")
        try:
            render(epd)
        except Exception:
            log.exception("Failed to render")
        log.info("Sleeping for %ds", REFRESH_INTERVAL)
        time.sleep(REFRESH_INTERVAL)


if __name__ == '__main__':
    main()
