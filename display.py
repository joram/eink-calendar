"""
E-ink display module. All hardware interaction lives here.

Usage:
    import display
    display.show(img_black, img_red)   # from a draw script

    python3 display.py                 # init and clear the screen
"""
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'e-Paper/RaspberryPi_JetsonNano/python/lib'))

from waveshare_epd import epd7in5b_V2

W, H = 800, 480


def _epd():
    return epd7in5b_V2.EPD()


def init():
    """Initialize and clear the display. Run once on first setup or to reset."""
    print("Initializing display...", flush=True)
    epd = _epd()
    epd.init()
    epd.Clear()
    epd.sleep()
    print("Done.", flush=True)


def show(img_black, img_red):
    """Wake the display, show the images, then sleep."""
    epd = _epd()
    epd.init()
    print("Displaying...", flush=True)
    epd.display(epd.getbuffer(img_black), epd.getbuffer(img_red))
    print("Done. Putting display to sleep.", flush=True)
    epd.sleep()


if __name__ == '__main__':
    init()
