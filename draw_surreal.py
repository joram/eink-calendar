import math
from PIL import Image, ImageDraw
import display

W, H = display.W, display.H

img_black = Image.new('1', (W, H), 255)
img_red   = Image.new('1', (W, H), 255)
b = ImageDraw.Draw(img_black)
r = ImageDraw.Draw(img_red)

# --- "The Eye Over the Desert" ---

# Sky / ground divide
horizon = H * 2 // 3
b.line([(0, horizon), (W, horizon)], fill=0, width=2)

# Ground texture: converging perspective lines to vanishing point
vp_x, vp_y = W // 2, horizon
for i in range(-6, 7):
    end_x = vp_x + i * 120
    b.line([(end_x, H), (vp_x, vp_y)], fill=0, width=1)

# Horizontal ground lines (foreshortened)
for i, y in enumerate(range(horizon + 20, H, 18 + i * 4)):
    b.line([(0, y), (W, y)], fill=0, width=1)

# Large floating eye — dominant element, upper center
ex, ey = W // 2, H // 3 - 10
erx, ery = 170, 85

# Eyelid shape (almond) via polygon
steps = 60
top_pts = [(ex + int(erx * math.cos(math.pi + math.pi * t / steps)),
            ey + int(ery * 0.7 * math.sin(math.pi * t / steps)))
           for t in range(steps + 1)]
bot_pts = [(ex + int(erx * math.cos(math.pi * t / steps)),
            ey + int(ery * math.sin(math.pi * t / steps)))
           for t in range(steps + 1)]
eye_poly = top_pts + bot_pts
b.polygon(eye_poly, outline=0, fill=255)
b.line(eye_poly + [eye_poly[0]], fill=0, width=4)

# Red iris
iris_r = 58
r.ellipse((ex - iris_r, ey - iris_r, ex + iris_r, ey + iris_r), fill=0)

# Black pupil
pupil_r = 26
b.ellipse((ex - pupil_r, ey - pupil_r, ex + pupil_r, ey + pupil_r), fill=0)

# Highlight glint (white dot in pupil — leave as background)

# Eyelashes radiating outward
for deg in range(-75, 76, 15):
    rad = math.radians(deg)
    # Upper lashes
    x1 = ex + int(erx * 0.95 * math.cos(math.pi - abs(rad)) * (-1 if deg < 0 else 1))
    y1 = ey - int(ery * 0.85 * abs(math.cos(rad)))
    x2 = ex + int((erx + 28) * math.cos(math.pi - abs(rad)) * (-1 if deg < 0 else 1))
    y2 = ey - int((ery + 28) * abs(math.cos(rad)) + 10)
    b.line([(x1, y1), (x2, y2)], fill=0, width=2)

# Shadow below eye (ellipse)
b.ellipse((ex - 90, ey + ery - 5, ex + 90, ey + ery + 20), fill=0)

# --- Melting clock, left side ---
cx, cy = 140, 210
cw, ch = 110, 75
b.rectangle([cx, cy, cx + cw, cy + ch], outline=0, width=3)
# Clock face marks
for mark in range(12):
    angle = math.radians(mark * 30 - 90)
    mx1 = cx + cw // 2 + int(30 * math.cos(angle))
    my1 = cy + ch // 2 + int(30 * math.sin(angle))
    mx2 = cx + cw // 2 + int(36 * math.cos(angle))
    my2 = cy + ch // 2 + int(36 * math.sin(angle))
    b.line([(mx1, my1), (mx2, my2)], fill=0, width=2)
# Hands showing 10:10
for hand_angle, hand_len in [(-60, 26), (60, 32)]:
    ha = math.radians(hand_angle - 90)
    b.line([(cx + cw // 2, cy + ch // 2),
            (cx + cw // 2 + int(hand_len * math.cos(ha)),
             cy + ch // 2 + int(hand_len * math.sin(ha)))], fill=0, width=2)
# Melt drip off bottom-right corner
drip = [
    (cx + cw - 30, cy + ch),
    (cx + cw - 10, cy + ch),
    (cx + cw + 5,  cy + ch + 40),
    (cx + cw - 5,  cy + ch + 70),
    (cx + cw - 20, cy + ch + 70),
    (cx + cw - 35, cy + ch + 40),
]
b.polygon(drip, outline=0, fill=255)
b.line(drip + [drip[0]], fill=0, width=2)

# Red shadow beneath clock
r.ellipse((cx + 5, cy + ch + 65, cx + cw + 10, cy + ch + 80), fill=0)

# --- Impossible floating cube, right side ---
# Front face
cube_x, cube_y = 590, 160
cs = 70  # cube side
depth = 30
front = [(cube_x, cube_y), (cube_x + cs, cube_y),
         (cube_x + cs, cube_y + cs), (cube_x, cube_y + cs)]
b.polygon(front, outline=0, fill=255)
b.line(front + [front[0]], fill=0, width=3)
# Top face
top_face = [
    (cube_x, cube_y),
    (cube_x + depth, cube_y - depth),
    (cube_x + cs + depth, cube_y - depth),
    (cube_x + cs, cube_y),
]
b.polygon(top_face, outline=0, fill=255)
b.line(top_face + [top_face[0]], fill=0, width=3)
# Right face
right_face = [
    (cube_x + cs, cube_y),
    (cube_x + cs + depth, cube_y - depth),
    (cube_x + cs + depth, cube_y + cs - depth),
    (cube_x + cs, cube_y + cs),
]
b.polygon(right_face, outline=0, fill=255)
b.line(right_face + [right_face[0]], fill=0, width=3)
# Red face fill on front
r.polygon(front, fill=0)
# Black grid lines over red front face
for i in range(1, 3):
    frac = cs * i // 3
    b.line([(cube_x + frac, cube_y), (cube_x + frac, cube_y + cs)], fill=0, width=1)
    b.line([(cube_x, cube_y + frac), (cube_x + cs, cube_y + frac)], fill=0, width=1)

# --- Small floating fish (surreal touch) ---
fx, fy = 420, horizon - 55
b.ellipse((fx, fy, fx + 90, fy + 32), outline=0, width=2)
b.polygon([(fx + 90, fy + 16), (fx + 118, fy + 2), (fx + 118, fy + 30)], outline=0, width=2)
b.ellipse((fx + 12, fy + 9, fx + 24, fy + 21), fill=0)
# Legs
for lx in [fx + 22, fx + 44, fx + 66]:
    b.line([(lx, fy + 32), (lx - 6, fy + 55)], fill=0, width=2)
    b.line([(lx - 6, fy + 55), (lx + 8, fy + 55)], fill=0, width=2)

# --- Red moon, upper right ---
mx, my = 700, 75
r.ellipse((mx - 38, my - 38, mx + 38, my + 38), fill=0)
# Crescent cutout suggestion via black arc chunk
b.ellipse((mx - 15, my - 45, mx + 42, my + 28), fill=0)

# --- Black sun, upper left, with rays ---
sx, sy, sr = 95, 70, 32
b.ellipse((sx - sr, sy - sr, sx + sr, sy + sr), fill=0)
for deg in range(0, 360, 30):
    angle = math.radians(deg)
    b.line([
        (sx + int((sr + 8) * math.cos(angle)), sy + int((sr + 8) * math.sin(angle))),
        (sx + int((sr + 22) * math.cos(angle)), sy + int((sr + 22) * math.sin(angle))),
    ], fill=0, width=2)

display.show(img_black, img_red)
