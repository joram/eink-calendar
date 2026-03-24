import sys
import os
import random
import glob

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'e-Paper/RaspberryPi_JetsonNano/python/lib'))

from waveshare_epd import epd7in5b_V2
from PIL import Image
import numpy as np

W, H = 800, 480
IMAGES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'images')

# Palette: index 0=white, 1=black, 2=red, rest=white (for quantize overflow)
_PALETTE_DATA = [255,255,255, 0,0,0, 255,0,0] + [255,255,255] * (256 - 3)
_PALETTE = Image.new('P', (1, 1))
_PALETTE.putpalette(_PALETTE_DATA)


def load_random_image():
    paths = glob.glob(os.path.join(IMAGES_DIR, '*'))
    if not paths:
        raise FileNotFoundError(f'No images found in {IMAGES_DIR}')
    path = random.choice(paths)
    print(f'Loading {os.path.basename(path)} ...', flush=True)
    return Image.open(path).convert('RGB').resize((W, H))


def to_eink_layers(img):
    """Floyd-Steinberg dither to 3-color palette, return (img_black, img_red)."""
    q = img.quantize(palette=_PALETTE, dither=1)
    arr = np.array(q)  # (H, W), values 0/1/2/...

    img_black = Image.fromarray(np.where(arr == 1, 0, 255).astype(np.uint8), 'L').convert('1')
    img_red   = Image.fromarray(np.where(arr == 2, 0, 255).astype(np.uint8), 'L').convert('1')
    return img_black, img_red


def main():
    img = load_random_image()

    print('Converting to e-ink palette...', flush=True)
    img_black, img_red = to_eink_layers(img)

    print('Initializing display...', flush=True)
    epd = epd7in5b_V2.EPD()
    epd.init()
    epd.Clear()

    print('Displaying...', flush=True)
    epd.display(epd.getbuffer(img_black), epd.getbuffer(img_red))
    print('Done. Putting display to sleep.', flush=True)
    epd.sleep()


if __name__ == '__main__':
    main()
