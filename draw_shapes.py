import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'e-Paper/RaspberryPi_JetsonNano/python/lib'))

from waveshare_epd import epd7in5b_V2
from PIL import Image, ImageDraw

epd = epd7in5b_V2.EPD()

print("Initializing display...", flush=True)
epd.init()
epd.Clear()

W, H = epd.width, epd.height  # 800 x 480

img_black = Image.new('1', (W, H), 255)
img_red   = Image.new('1', (W, H), 255)
b = ImageDraw.Draw(img_black)
r = ImageDraw.Draw(img_red)

# Black rectangle (top-left)
b.rectangle([40, 40, 200, 160], outline=0, width=4)

# Black circle (top-right)
b.ellipse([600, 40, 760, 200], outline=0, width=4)

# Red filled triangle (bottom-left)
r.polygon([(40, 440), (200, 440), (120, 300)], fill=0)

# Red filled circle (bottom-right)
r.ellipse([600, 300, 760, 440], fill=0)

# Black horizontal line across the middle
b.line([(40, H // 2), (W - 40, H // 2)], fill=0, width=3)

# Black diagonal cross in center
cx, cy = W // 2, H // 2
b.line([(cx - 60, cy - 60), (cx + 60, cy + 60)], fill=0, width=3)
b.line([(cx + 60, cy - 60), (cx - 60, cy + 60)], fill=0, width=3)

print("Displaying...", flush=True)
epd.display(epd.getbuffer(img_black), epd.getbuffer(img_red))

print("Done. Putting display to sleep.", flush=True)
epd.sleep()
