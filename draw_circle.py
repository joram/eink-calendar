from PIL import Image, ImageDraw
import display

W, H = display.W, display.H

img_black = Image.new('1', (W, H), 255)
img_red   = Image.new('1', (W, H), 255)
draw_black = ImageDraw.Draw(img_black)
draw_red   = ImageDraw.Draw(img_red)

# Black circle outline in the center
cx, cy, r = W // 2, H // 2, 100
draw_black.ellipse((cx - r, cy - r, cx + r, cy + r), outline=0, width=4)

# Smaller red filled circle offset slightly
r2 = 40
draw_red.ellipse((cx + 80 - r2, cy - r2, cx + 80 + r2, cy + r2), fill=0)

display.show(img_black, img_red)
