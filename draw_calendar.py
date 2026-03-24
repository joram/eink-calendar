import os
import calendar
import datetime

from PIL import Image, ImageDraw, ImageFont
import gcal
import display

# --- Display dimensions ---
W, H = display.W, display.H

# --- Layout ---
LEFT_W = 240          # left panel (month calendar) width
DIVIDER_X = LEFT_W
RIGHT_X = LEFT_W + 12  # right panel content start
PAD = 12               # general padding

# Month grid cell dimensions
CELL_W = (LEFT_W - 2 * PAD) // 7  # ~30px
CELL_H = 38

# --- Fonts ---
_FONT_DIR = '/usr/share/fonts/truetype/dejavu'


def font(size, bold=False):
    name = 'DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf'
    try:
        return ImageFont.truetype(os.path.join(_FONT_DIR, name), size)
    except OSError:
        return ImageFont.load_default()


def text_size(draw, text, fnt):
    bb = draw.textbbox((0, 0), text, font=fnt)
    return bb[2] - bb[0], bb[3] - bb[1]


# --- Month panel ---

def draw_month(b, r, today):
    f_title    = font(20, bold=True)
    f_dayname  = font(13)
    f_num      = font(15)
    f_num_bold = font(15, bold=True)

    # Month/year header
    title = today.strftime('%B %Y')
    b.text((PAD, PAD), title, font=f_title, fill=0)

    # Separator line
    b.line([(PAD, 38), (LEFT_W - PAD, 38)], fill=0, width=1)

    # Day-of-week names (Monday first)
    day_names = ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']
    y = 44
    for i, name in enumerate(day_names):
        tw, _ = text_size(b, name, f_dayname)
        x = PAD + i * CELL_W + (CELL_W - tw) // 2
        b.text((x, y), name, font=f_dayname, fill=0)

    # Date grid
    y = 64
    for week in calendar.monthcalendar(today.year, today.month):
        for col, day in enumerate(week):
            if day == 0:
                continue
            date = datetime.date(today.year, today.month, day)
            is_today = (date == today)
            day_str = str(day)
            fnt = f_num_bold if is_today else f_num

            cx = PAD + col * CELL_W + CELL_W // 2
            cy = y + CELL_H // 2
            tw, th = text_size(b, day_str, fnt)

            if is_today:
                # Red filled circle with white text punched out
                r.ellipse((cx - 15, cy - 15, cx + 15, cy + 15), fill=0)
                r.text((cx - tw // 2, cy - th // 2), day_str, font=fnt, fill=255)
            else:
                b.text((cx - tw // 2, cy - th // 2), day_str, font=fnt, fill=0)
        y += CELL_H

    # Vertical divider between panels
    b.line([(DIVIDER_X, 0), (DIVIDER_X, H)], fill=0, width=1)


# --- Events panel ---

def draw_events(b, r, events_by_date, today):
    f_section = font(17, bold=True)
    f_time    = font(13)
    f_title   = font(14)
    f_empty   = font(13)

    x0 = RIGHT_X
    y = PAD
    max_y = H - PAD
    time_col_w = 70  # fixed width for time column

    for offset in range(3):
        date = today + datetime.timedelta(days=offset)
        if y + 30 > max_y:
            break

        # Section header
        if offset == 0:
            label = 'Today \u2014 ' + date.strftime('%A, %B %-d')
            draw_label = r  # red for today
        elif offset == 1:
            label = 'Tomorrow \u2014 ' + date.strftime('%A, %B %-d')
            draw_label = b
        else:
            label = date.strftime('%A, %B %-d')
            draw_label = b

        draw_label.text((x0, y), label, font=f_section, fill=0)
        y += 24
        b.line([(x0, y), (W - PAD, y)], fill=0, width=1)
        y += 5

        # Events
        events = events_by_date.get(date, [])
        if not events:
            b.text((x0 + 6, y), 'No events', font=f_empty, fill=0)
            y += 18
        else:
            for event in events:
                if y + 18 > max_y:
                    break
                time_str = event['time']
                title = event['title']

                b.text((x0, y), time_str, font=f_time, fill=0)

                # Truncate title to fit remaining width
                title_x = x0 + time_col_w
                max_w = W - title_x - PAD
                original = title
                while title and text_size(b, title, f_title)[0] > max_w:
                    title = title[:-1]
                if len(title) < len(original):
                    title = title.rstrip() + '\u2026'

                b.text((title_x, y), title, font=f_title, fill=0)
                y += 20

        y += 12  # gap between sections


# --- Main ---

def main():
    print("Fetching calendar events...", flush=True)
    today = datetime.date.today()
    try:
        events = gcal.get_events(days=3)
    except Exception as e:
        print(f"Warning: could not fetch events: {e}", flush=True)
        events = {}

    img_black = Image.new('1', (W, H), 255)
    img_red   = Image.new('1', (W, H), 255)
    b = ImageDraw.Draw(img_black)
    r = ImageDraw.Draw(img_red)

    draw_month(b, r, today)
    draw_events(b, r, events, today)

    display.show(img_black, img_red)


if __name__ == '__main__':
    main()
