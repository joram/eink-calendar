import sys
import os

# Add Waveshare library to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'e-Paper/RaspberryPi_JetsonNano/python/lib'))

from waveshare_epd import epd7in5b_V2
from PIL import Image, ImageDraw

epd = epd7in5b_V2.EPD()

print("Initializing display...")
epd.init()
epd.Clear()

# Create two images: black layer and red layer
# Display is 800x480
img_black = Image.new('1', (epd.width, epd.height), 255)  # white background
img_red   = Image.new('1', (epd.width, epd.height), 255)  # white background

draw_black = ImageDraw.Draw(img_black)
draw_red   = ImageDraw.Draw(img_red)

# Draw a black circle outline in the center
cx, cy, r = epd.width // 2, epd.height // 2, 100
draw_black.ellipse((cx - r, cy - r, cx + r, cy + r), outline=0, width=4)

# Draw a smaller red filled circle offset slightly
r2 = 40
draw_red.ellipse((cx + 80 - r2, cy - r2, cx + 80 + r2, cy + r2), fill=0)

print("Displaying...")
epd.display(epd.getbuffer(img_black), epd.getbuffer(img_red))

print("Done. Putting display to sleep.")
epd.sleep()
