#include "calendar_view.h"

#include "epd7in5b_V2.h"
#include "red_scale_logo_bitmap.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <time.h>

namespace {

constexpr int W = 800;
constexpr int H = 480;
constexpr int PAD = 12;
/** Space between the first day heading and the top-right logo bitmap. */
constexpr int kLogoTextGapPx = 8;

static int dayHeaderLabelMaxWidthPx(int dayOffset) {
    const int base = W - 2 * PAD;
    if (dayOffset == 0) {
        const int reserve = kRedScaleLogoW + kLogoTextGapPx;
        const int mw = base - reserve;
        return mw > 80 ? mw : 80;
    }
    return base;
}

/** Hand-drawn logo; clipped when rendering horizontal bands. */
static void drawRedScaleLogoClipped(GFXcanvas1& b, GFXcanvas1& red, int bandY0, int bandH) {
    const int lx = W - PAD - kRedScaleLogoW;
    const int ly = PAD;
    const int y1 = ly + kRedScaleLogoH;
    const int by1 = bandY0 + bandH;
    const int clipY0 = ly > bandY0 ? ly : bandY0;
    const int clipY1 = y1 < by1 ? y1 : by1;
    if (clipY0 >= clipY1) {
        return;
    }
    const int skipRows = clipY0 - ly;
    const int drawH = clipY1 - clipY0;
    const int dstY = clipY0 - bandY0;
    const size_t rowBytes = static_cast<size_t>(kRedScaleLogoBytesPerRow);
    b.drawBitmap(lx, dstY, kRedScaleLogoBlack + skipRows * rowBytes, kRedScaleLogoW, drawH, 0, 1);
    red.drawBitmap(lx, dstY, kRedScaleLogoRed + skipRows * rowBytes, kRedScaleLogoW, drawH, 0, 1);
}

/** Horizontal lines stop short of the logo when they pass through the top-right graphic. */
static int bodyLineEndX(int dayOffset, int lineScreenY) {
    if (dayOffset != 0 || lineScreenY >= PAD + kRedScaleLogoH) {
        return W - PAD;
    }
    const int xe = W - PAD - kRedScaleLogoW - kLogoTextGapPx;
    return xe > PAD + 120 ? xe : (W - PAD);
}

/** First-day event rows under the logo use a narrower title column so text does not cover the art. */
static int maxTitleWidthPx(int dayOffset, int rowScreenY, int titleColX) {
    int mw = W - PAD - titleColX;
    if (dayOffset != 0 || rowScreenY >= PAD + kRedScaleLogoH) {
        return mw;
    }
    const int logoLeft = W - PAD - kRedScaleLogoW;
    if (titleColX >= logoLeft) {
        return mw;
    }
    const int cap = logoLeft - kLogoTextGapPx - titleColX;
    if (cap > 48 && cap < mw) {
        return cap;
    }
    return mw;
}

static int placeholderMaxWidthPx(int dayOffset, int rowScreenY, int bodyLeftPad) {
    int mw = W - (PAD + bodyLeftPad) - PAD;
    if (dayOffset != 0 || rowScreenY >= PAD + kRedScaleLogoH) {
        return mw;
    }
    const int logoLeft = W - PAD - kRedScaleLogoW;
    const int textLeft = PAD + bodyLeftPad;
    if (textLeft >= logoLeft) {
        return mw;
    }
    const int cap = logoLeft - kLogoTextGapPx - textLeft;
    if (cap > 48 && cap < mw) {
        return cap;
    }
    return mw;
}

/** Caller-owned buffer; same pattern as main.cpp GFXcanvas1Static. */
class LocalCanvas : public GFXcanvas1 {
public:
    LocalCanvas(uint16_t w, uint16_t h, uint8_t* ext) : GFXcanvas1(w, h, false) {
        buffer = ext;
        if (ext) {
            memset(ext, 0, ((static_cast<uint32_t>(w) + 7U) / 8U) * static_cast<uint32_t>(h));
        }
    }
};

uint16_t textWidth(GFXcanvas1& c, const char* s, uint8_t sz) {
    c.setTextSize(sz);
    int16_t x1, y1;
    uint16_t tw, th;
    c.getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
    return tw;
}

/** Line height for one row of body text at the given scale (classic 5×7 font). */
int rowStepPx(GFXcanvas1& c, uint8_t sz) {
    int16_t x1, y1;
    uint16_t tw, th;
    c.setTextSize(sz);
    c.getTextBounds("Ay", 0, 0, &x1, &y1, &tw, &th);
    return static_cast<int>(th) + (sz >= 3 ? 4 : 2);
}

/** Space after each event row (below the title baseline area). */
constexpr int kBelowEventTitleGapPx = 8;
/** Vertical space around the dashed rule between all-day and timed events (1px line between gaps). */
constexpr int kAllDayTimedSepGapBeforePx = 4;
constexpr int kAllDayTimedSepGapAfterPx = 4;
constexpr int kAllDayTimedSeparatorTotalPx =
    kAllDayTimedSepGapBeforePx + 1 + kAllDayTimedSepGapAfterPx;
/** Gap between reserved meta column and aligned event titles. */
constexpr int kBetweenColGapPx = 2;
/** Max width reserved for time+abbrev (px); caps the % so titles sit closer than a full 40% column. */
constexpr int kMetaColumnMaxPx = 175;
/** Vertical gap after each day section (before the next day heading). */
constexpr int kBetweenDaySectionsGapPx = 22;
/** Font scale for time + calendar abbrev (left column). */
constexpr uint8_t kMetaColTextSize = 2;

/** Event title is never smaller than the time column — crowded layouts only shrink day headers. */
static uint8_t eventTitleDrawSize(uint8_t sectionTextSz) {
    return sectionTextSz > kMetaColTextSize ? sectionTextSz : kMetaColTextSize;
}

/** Vertical space for one event row: one line, height = max(meta, title) + gap below. */
int eventBlockHeightPx(GFXcanvas1& c, uint8_t sectionTextSz) {
    const int metaH = rowStepPx(c, kMetaColTextSize);
    const int titleH = rowStepPx(c, eventTitleDrawSize(sectionTextSz));
    return (metaH > titleH ? metaH : titleH) + kBelowEventTitleGapPx;
}

void formatDayLabel(int offset, const struct tm& dayTm, char* label, size_t labelLen) {
    if (offset == 0) {
        strftime(label, labelLen, "Today - %A, %B %e", &dayTm);
    } else if (offset == 1) {
        strftime(label, labelLen, "Tomorrow - %A, %B %e", &dayTm);
    } else {
        strftime(label, labelLen, "%A, %B %e", &dayTm);
    }
}

/**
 * Local calendar date for today + dayOffset (0 = today), anchored at local midnight so weekday
 * matches google_calendar dayStrFromOffset / API buckets (avoids mktime quirks from "now" + mday).
 */
static void calendarDayAtLocalMidnight(int dayOffset, struct tm* out) {
    time_t now = time(nullptr);
    localtime_r(&now, out);
    out->tm_hour = 0;
    out->tm_min = 0;
    out->tm_sec = 0;
    out->tm_isdst = -1;
    out->tm_mday += dayOffset;
    mktime(out);
}

static bool eventIsAllDay(const CalendarEvent& ev) {
    return strcmp(ev.timeStr, "All day") == 0;
}

static void drawDashedHLine(GFXcanvas1& b, int x0, int x1, int y) {
    constexpr int kDashPx = 6;
    constexpr int kGapPx = 4;
    int x = x0;
    while (x < x1) {
        const int segEnd = x + kDashPx;
        const int xe = segEnd < x1 ? segEnd : x1;
        if (xe > x) {
            b.drawLine(x, y, xe - 1, y, 0);
        }
        x = xe + kGapPx;
    }
}

/** Day/date line uses 2× section body scale (capped); may shrink if the string is too wide. */
static uint8_t dayHeaderTextSize(uint8_t sectionTextSz) {
    int s = static_cast<int>(sectionTextSz) * 2;
    constexpr int kMax = 6;
    if (s > kMax) {
        s = kMax;
    }
    return static_cast<uint8_t>(s);
}

static uint8_t dayHeaderTextSizeForLabel(GFXcanvas1& c, const char* label, uint8_t sectionTextSz, int maxW) {
    uint8_t sz = dayHeaderTextSize(sectionTextSz);
    while (sz > sectionTextSz && textWidth(c, label, sz) > static_cast<uint16_t>(maxW)) {
        sz--;
    }
    return sz;
}

/** Pixel height of day header line + rule + gap (matches @ref dayHeaderBlockPx). */
static int dayHeaderRuleAndBodyStartDeltaPx(GFXcanvas1& c, const char* label, uint8_t labelSz) {
    c.setTextSize(labelSz);
    int16_t x1, y1;
    uint16_t tw, th;
    c.getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
    return static_cast<int>(th) + 1 + 5;
}

int dayHeaderBlockPx(GFXcanvas1& b, GFXcanvas1& red, int offset, uint8_t sectionTextSz) {
    struct tm dayTm;
    calendarDayAtLocalMidnight(offset, &dayTm);
    char label[72];
    formatDayLabel(offset, dayTm, label, sizeof(label));
    const uint8_t labelSz =
        dayHeaderTextSizeForLabel(b, label, sectionTextSz, dayHeaderLabelMaxWidthPx(offset));
    GFXcanvas1& m = (offset == 0) ? red : b;
    m.setTextSize(labelSz);
    int16_t x1, y1;
    uint16_t tw, th;
    m.getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
    return static_cast<int>(th) + 1 + 5;
}

/** Preferred UI scale (1…3) from total event count — larger totals use smaller text. */
static uint8_t mapTotalEventsToScale(int totalEvents) {
    if (totalEvents <= 0) {
        return 3;
    }
    if (totalEvents <= 8) {
        return 3;
    }
    if (totalEvents <= 16) {
        return 2;
    }
    return 1;
}

/**
 * Day/date headings render at 2× the section body scale (see @ref dayHeaderTextSizeForLabel).
 * Placeholders and event rows use section scale from total event count
 * (@ref mapTotalEventsToScale). Layout fit uses @ref dayHeaderBlockPx and @ref eventBlockHeightPx.
 */
uint8_t pickSectionTextSize(GFXcanvas1& b, GFXcanvas1& red, const DayEvents threeDays[3], bool hadFetch) {
    constexpr uint8_t kMaxSectionTextSize = 3;
    const int maxY = H - PAD;

    int totalEvents = 0;
    for (int d = 0; d < 3; d++) {
        totalEvents += threeDays[d].count;
    }

    uint8_t startSz = kMaxSectionTextSize;
    if (hadFetch && totalEvents > 0) {
        startSz = mapTotalEventsToScale(totalEvents);
    }

    for (uint8_t textSz = startSz; textSz >= 1; textSz--) {
        int y = PAD;
        bool fits = true;
        for (int offset = 0; offset < 3; offset++) {
            y += dayHeaderBlockPx(b, red, offset, textSz);
            const int rowSingle = rowStepPx(b, textSz);
            const int rowEvent = eventBlockHeightPx(b, textSz);
            int blockH = 0;
            if (!hadFetch) {
                blockH = rowSingle;
            } else if (threeDays[offset].count == 0) {
                blockH = rowSingle;
            } else {
                int extraSep = 0;
                const DayEvents& de = threeDays[offset];
                if (de.count > 0) {
                    int nAll = 0;
                    for (int i = 0; i < de.count; i++) {
                        if (eventIsAllDay(de.items[i])) {
                            nAll++;
                        }
                    }
                    const int nTimed = de.count - nAll;
                    if (nAll > 0 && nTimed > 0) {
                        extraSep = kAllDayTimedSeparatorTotalPx;
                    }
                }
                blockH = de.count * rowEvent + extraSep + rowSingle;
            }
            y += blockH;
            y += kBetweenDaySectionsGapPx;
            if (y > maxY) {
                fits = false;
                break;
            }
        }
        if (fits) {
            return textSz;
        }
    }
    return 1;
}

/** Truncate with "..." so the rendered width never exceeds maxW. */
void truncateWithEllipsis(GFXcanvas1& c, const char* src, char* dst, size_t dstCap, uint8_t sz, int maxW) {
    if (dstCap == 0) {
        return;
    }
    if (maxW <= 0) {
        dst[0] = 0;
        return;
    }
    strlcpy(dst, src, dstCap);
    if (textWidth(c, dst, sz) <= static_cast<uint16_t>(maxW)) {
        return;
    }

    static const char kEll[] = "...";
    const size_t ellLen = strlen(kEll);
    const uint16_t ellW = textWidth(c, kEll, sz);
    if (static_cast<int>(ellW) > maxW) {
        dst[0] = 0;
        return;
    }
    const int maxCorePx = maxW - static_cast<int>(ellW);

    while (strlen(dst) > 0 && textWidth(c, dst, sz) > static_cast<uint16_t>(maxCorePx)) {
        dst[strlen(dst) - 1] = 0;
    }
    strlcat(dst, kEll, dstCap);
    while (textWidth(c, dst, sz) > static_cast<uint16_t>(maxW) && strlen(dst) > ellLen) {
        const size_t L = strlen(dst);
        dst[L - ellLen - 1] = 0;
        strlcat(dst, kEll, dstCap);
    }
}

/**
 * One-line footer when a day has more events than fit vertically.
 * @param bandH 0 = full canvas (y is absolute); else band strip [bandY0, bandY0+bandH).
 */
static void drawOverflowEventsFooter(GFXcanvas1& b, int dayOffset, int& y, int maxY, int omitted,
                                     uint8_t textSz, int bodyLeftPad, int rowStep, int bandY0,
                                     int bandH) {
    if (omitted <= 0) {
        return;
    }
    if (y + rowStep > maxY) {
        return;
    }
    char raw[40];
    snprintf(raw, sizeof(raw), "... (%d more) ...", omitted);
    char line[48];
    const int pw = placeholderMaxWidthPx(dayOffset, y, bodyLeftPad);
    truncateWithEllipsis(b, raw, line, sizeof(line), textSz, pw);
    b.setTextSize(textSz);
    b.setTextColor(0);
    const int xCur = PAD + bodyLeftPad;
    if (bandH <= 0) {
        b.setCursor(xCur, y);
        b.print(line);
    } else {
        const int bandY1 = bandY0 + bandH;
        if (y + rowStep > bandY0 && y < bandY1) {
            b.setCursor(xCur, y - bandY0);
            b.print(line);
        }
    }
    y += rowStep;
}

/** Event titles use the same scale for every row; long titles only get "..." at that scale. */
static void drawEventTitle(GFXcanvas1& b, int titleColX, int y, const char* title, int maxTitleW,
                           uint8_t titleTextSz) {
    if (maxTitleW <= 0) {
        return;
    }
    const int mw = maxTitleW > 8 ? maxTitleW : 8;
    b.setTextColor(0);
    if (textWidth(b, title, titleTextSz) <= static_cast<uint16_t>(mw)) {
        b.setTextSize(titleTextSz);
        b.setCursor(titleColX, y);
        b.print(title);
        return;
    }
    char buf[sizeof(CalendarEvent::title)];
    truncateWithEllipsis(b, title, buf, sizeof(buf), titleTextSz, mw);
    b.setTextSize(titleTextSz);
    b.setCursor(titleColX, y);
    b.print(buf);
}

/** Time on black layer; calendar abbrev on red layer (e-paper red ink). Fits within @a metaColMaxW px. */
void drawMetaTimeAndCal(GFXcanvas1& b, GFXcanvas1& red, int x0, int y, const CalendarEvent& ev,
                        int metaColMaxW) {
    b.setTextSize(kMetaColTextSize);
    b.setTextColor(0);
    red.setTextSize(kMetaColTextSize);
    red.setTextColor(0);

    char timeBuf[24];
    strlcpy(timeBuf, ev.timeStr, sizeof(timeBuf));

    if (!ev.calAbbrev[0]) {
        truncateWithEllipsis(b, timeBuf, timeBuf, sizeof(timeBuf), kMetaColTextSize, metaColMaxW);
        b.setCursor(x0, y);
        b.print(timeBuf);
        return;
    }

    const uint16_t spaceW = textWidth(b, " ", kMetaColTextSize);
    uint16_t tw = textWidth(b, timeBuf, kMetaColTextSize);
    int roomForCal = static_cast<int>(metaColMaxW) - static_cast<int>(tw) - static_cast<int>(spaceW);
    constexpr int kMinCalPx = 16;

    if (roomForCal < kMinCalPx) {
        const int timeMax =
            static_cast<int>(metaColMaxW) - kMinCalPx - static_cast<int>(spaceW);
        if (timeMax > 24) {
            truncateWithEllipsis(b, timeBuf, timeBuf, sizeof(timeBuf), kMetaColTextSize, timeMax);
            tw = textWidth(b, timeBuf, kMetaColTextSize);
            roomForCal = static_cast<int>(metaColMaxW) - static_cast<int>(tw) - static_cast<int>(spaceW);
        }
    }
    if (roomForCal < 8) {
        roomForCal = 8;
    }

    char calBuf[sizeof(ev.calAbbrev)];
    truncateWithEllipsis(red, ev.calAbbrev, calBuf, sizeof(calBuf), kMetaColTextSize, roomForCal);

    b.setCursor(x0, y);
    b.print(timeBuf);
    b.print(' ');
    red.setCursor(x0 + static_cast<int>(tw) + static_cast<int>(spaceW), y);
    red.print(calBuf);
}

void drawEvents(GFXcanvas1& b, GFXcanvas1& red, const DayEvents threeDays[3], bool hadFetch) {
    const int x0 = PAD;
    int y = PAD;
    const int maxY = H - PAD;

    const uint8_t textSz = pickSectionTextSize(b, red, threeDays, hadFetch);
    const int rowStep = rowStepPx(b, textSz);
    const int metaStep = rowStepPx(b, kMetaColTextSize);
    const int titleRowStep = rowStepPx(b, eventTitleDrawSize(textSz));
    const int eventBlockH = eventBlockHeightPx(b, textSz);

    for (int offset = 0; offset < 3; offset++) {
        struct tm dayTm;
        calendarDayAtLocalMidnight(offset, &dayTm);

        char label[72];
        formatDayLabel(offset, dayTm, label, sizeof(label));

        const int headerPx = dayHeaderBlockPx(b, red, offset, textSz);
        if (y + headerPx > maxY) {
            break;
        }

        const int yDayStart = y;
        int16_t bx1, by1;
        uint16_t btw, bth;
        const uint8_t headerTextSz =
            dayHeaderTextSizeForLabel(b, label, textSz, dayHeaderLabelMaxWidthPx(offset));
        if (offset == 0) {
            red.setTextSize(headerTextSz);
            red.setTextColor(0);
            red.setCursor(x0, yDayStart);
            red.print(label);
            red.getTextBounds(label, x0, yDayStart, &bx1, &by1, &btw, &bth);
        } else {
            b.setTextSize(headerTextSz);
            b.setTextColor(0);
            b.setCursor(x0, yDayStart);
            b.print(label);
            b.getTextBounds(label, x0, yDayStart, &bx1, &by1, &btw, &bth);
        }
        const int lineY = by1 + static_cast<int>(bth);
        b.drawLine(x0, lineY, bodyLineEndX(offset, lineY), lineY, 0);
        y = lineY + 1 + 5;

        b.setTextSize(textSz);
        b.setTextColor(0);

        const DayEvents& day = threeDays[offset];
        const int bodyLeftPad = 6;

        if (!hadFetch) {
            char line[48];
            const int placeholderMaxW = placeholderMaxWidthPx(offset, y, bodyLeftPad);
            truncateWithEllipsis(b, "(Calendar unavailable)", line, sizeof(line), textSz, placeholderMaxW);
            b.setCursor(x0 + bodyLeftPad, y);
            b.print(line);
            y += rowStep;
        } else if (day.count == 0) {
            char line[32];
            const int placeholderMaxW = placeholderMaxWidthPx(offset, y, bodyLeftPad);
            truncateWithEllipsis(b, "No events", line, sizeof(line), textSz, placeholderMaxW);
            b.setCursor(x0 + bodyLeftPad, y);
            b.print(line);
            y += rowStep;
        } else {
            const int bodyW = W - x0 - PAD;
            int metaColMaxW = (bodyW * 36) / 100;
            if (metaColMaxW > kMetaColumnMaxPx) {
                metaColMaxW = kMetaColumnMaxPx;
            }
            if (metaColMaxW < 80) {
                metaColMaxW = 80;
            }
            const int titleColX = x0 + metaColMaxW + kBetweenColGapPx;
            const int rowH = (metaStep > titleRowStep) ? metaStep : titleRowStep;

            int nAllDay = 0;
            for (int i = 0; i < day.count; i++) {
                if (eventIsAllDay(day.items[i])) {
                    nAllDay++;
                }
            }
            const int nTimed = day.count - nAllDay;
            const bool allDayAndTimedSep = (nAllDay > 0 && nTimed > 0);

            int eventsDrawn = 0;
            bool clipped = false;
            for (int i = 0; i < day.count; i++) {
                if (!eventIsAllDay(day.items[i])) {
                    continue;
                }
                if (y + eventBlockH > maxY) {
                    clipped = true;
                    break;
                }
                const CalendarEvent& ev = day.items[i];
                drawMetaTimeAndCal(b, red, x0, y, ev, metaColMaxW);
                drawEventTitle(b, titleColX, y, ev.title, maxTitleWidthPx(offset, y, titleColX),
                               eventTitleDrawSize(textSz));
                y += rowH + kBelowEventTitleGapPx;
                eventsDrawn++;
            }

            if (!clipped && allDayAndTimedSep && y + kAllDayTimedSeparatorTotalPx <= maxY) {
                y += kAllDayTimedSepGapBeforePx;
                drawDashedHLine(b, x0, bodyLineEndX(offset, y), y);
                y += 1 + kAllDayTimedSepGapAfterPx;
            }

            if (!clipped) {
                for (int i = 0; i < day.count; i++) {
                    if (eventIsAllDay(day.items[i])) {
                        continue;
                    }
                    if (y + eventBlockH > maxY) {
                        clipped = true;
                        break;
                    }
                    const CalendarEvent& ev = day.items[i];
                    drawMetaTimeAndCal(b, red, x0, y, ev, metaColMaxW);
                    drawEventTitle(b, titleColX, y, ev.title, maxTitleWidthPx(offset, y, titleColX),
                                   eventTitleDrawSize(textSz));
                    y += rowH + kBelowEventTitleGapPx;
                    eventsDrawn++;
                }
            }
            const int omitted = day.count - eventsDrawn;
            drawOverflowEventsFooter(b, offset, y, maxY, omitted, textSz, bodyLeftPad, rowStep, 0, 0);
        }
        y += kBetweenDaySectionsGapPx;
    }
    drawRedScaleLogoClipped(b, red, 0, H);
}

/** Same layout as drawEvents, but only [bandY0, bandY0+bandH) in screen space; canvases are W×bandH. */
static void drawEventsBand(GFXcanvas1& b, GFXcanvas1& red, const DayEvents threeDays[3], bool hadFetch,
                           uint8_t textSz, int bandY0, int bandH) {
    const int x0 = PAD;
    const int bandY1 = bandY0 + bandH;
    int y = PAD;
    const int maxY = H - PAD;

    const int rowStep = rowStepPx(b, textSz);
    const int metaStep = rowStepPx(b, kMetaColTextSize);
    const int titleRowStep = rowStepPx(b, eventTitleDrawSize(textSz));
    const int eventBlockH = eventBlockHeightPx(b, textSz);
    const int rowH = (metaStep > titleRowStep) ? metaStep : titleRowStep;

    for (int offset = 0; offset < 3; offset++) {
        if (y >= bandY1) {
            break;
        }

        struct tm dayTm;
        calendarDayAtLocalMidnight(offset, &dayTm);

        char label[72];
        formatDayLabel(offset, dayTm, label, sizeof(label));

        const int headerPx = dayHeaderBlockPx(b, red, offset, textSz);
        if (y + headerPx > maxY) {
            break;
        }

        const DayEvents& day = threeDays[offset];

        const int yDayStart = y;
        const int yRel = yDayStart - bandY0;

        uint8_t headerTextSz =
            dayHeaderTextSizeForLabel(b, label, textSz, dayHeaderLabelMaxWidthPx(offset));
        if (yDayStart >= bandY0 && yDayStart < bandY1) {
            while (headerTextSz > textSz &&
                   yRel + dayHeaderRuleAndBodyStartDeltaPx(b, label, headerTextSz) > bandH) {
                headerTextSz--;
            }
        }

        int lineY = 0;
        if (yDayStart >= bandY0) {
            b.setTextSize(headerTextSz);
            int16_t bx1, by1;
            uint16_t btw, bth;
            b.getTextBounds(label, x0, yRel, &bx1, &by1, &btw, &bth);
            lineY = bandY0 + by1 + static_cast<int>(bth);
        } else {
            lineY = yDayStart + headerPx - 6;
        }
        const int yAfterRuler = lineY + 1 + 5;

        int yy = yAfterRuler;
        if (!hadFetch) {
            yy += rowStep;
        } else if (day.count == 0) {
            yy += rowStep;
        } else {
            int nAll = 0;
            for (int i = 0; i < day.count; i++) {
                if (eventIsAllDay(day.items[i])) {
                    nAll++;
                }
            }
            const int nTm = day.count - nAll;
            const int extraSep = (nAll > 0 && nTm > 0) ? kAllDayTimedSeparatorTotalPx : 0;
            yy += day.count * (rowH + kBelowEventTitleGapPx) + extraSep + rowStep;
        }
        yy += kBetweenDaySectionsGapPx;

        if (yy <= bandY0) {
            y = yy;
            continue;
        }
        if (yDayStart >= bandY1) {
            break;
        }

        if (yDayStart >= bandY0 && yDayStart < bandY1 && yRel >= 0 &&
            yRel + dayHeaderRuleAndBodyStartDeltaPx(b, label, headerTextSz) <= bandH) {
            if (offset == 0) {
                red.setTextSize(headerTextSz);
                red.setTextColor(0);
                red.setCursor(x0, yRel);
                red.print(label);
            } else {
                b.setTextSize(headerTextSz);
                b.setTextColor(0);
                b.setCursor(x0, yRel);
                b.print(label);
            }
        }
        if (lineY > bandY0 && lineY < bandY1) {
            b.drawLine(x0, lineY - bandY0, bodyLineEndX(offset, lineY), lineY - bandY0, 0);
        }

        if (yDayStart >= bandY0 && yDayStart < bandY1) {
            y = yDayStart + dayHeaderRuleAndBodyStartDeltaPx(b, label, headerTextSz);
        } else {
            y = yDayStart + headerPx;
        }

        b.setTextSize(textSz);
        b.setTextColor(0);

        const int bodyLeftPad = 6;

        if (!hadFetch) {
            char line[48];
            const int placeholderMaxW = placeholderMaxWidthPx(offset, y, bodyLeftPad);
            truncateWithEllipsis(b, "(Calendar unavailable)", line, sizeof(line), textSz, placeholderMaxW);
            if (y + rowStep > bandY0 && y < bandY1) {
                b.setCursor(x0 + bodyLeftPad, y - bandY0);
                b.print(line);
            }
            y += rowStep;
        } else if (day.count == 0) {
            char line[32];
            const int placeholderMaxW = placeholderMaxWidthPx(offset, y, bodyLeftPad);
            truncateWithEllipsis(b, "No events", line, sizeof(line), textSz, placeholderMaxW);
            if (y + rowStep > bandY0 && y < bandY1) {
                b.setCursor(x0 + bodyLeftPad, y - bandY0);
                b.print(line);
            }
            y += rowStep;
        } else {
            const int bodyW = W - x0 - PAD;
            int metaColMaxW = (bodyW * 36) / 100;
            if (metaColMaxW > kMetaColumnMaxPx) {
                metaColMaxW = kMetaColumnMaxPx;
            }
            if (metaColMaxW < 80) {
                metaColMaxW = 80;
            }
            const int titleColX = x0 + metaColMaxW + kBetweenColGapPx;

            int nAllDay = 0;
            for (int i = 0; i < day.count; i++) {
                if (eventIsAllDay(day.items[i])) {
                    nAllDay++;
                }
            }
            const int nTimed = day.count - nAllDay;
            const bool allDayAndTimedSep = (nAllDay > 0 && nTimed > 0);

            int eventsDrawnBand = 0;
            bool clippedBand = false;
            for (int i = 0; i < day.count; i++) {
                if (!eventIsAllDay(day.items[i])) {
                    continue;
                }
                if (y + eventBlockH > maxY) {
                    clippedBand = true;
                    break;
                }
                const CalendarEvent& ev = day.items[i];
                if (y + eventBlockH > bandY0 && y < bandY1) {
                    drawMetaTimeAndCal(b, red, x0, y - bandY0, ev, metaColMaxW);
                    drawEventTitle(b, titleColX, y - bandY0, ev.title,
                                   maxTitleWidthPx(offset, y, titleColX), eventTitleDrawSize(textSz));
                }
                y += rowH + kBelowEventTitleGapPx;
                eventsDrawnBand++;
            }

            if (!clippedBand && allDayAndTimedSep && y + kAllDayTimedSeparatorTotalPx <= maxY) {
                y += kAllDayTimedSepGapBeforePx;
                if (y >= bandY0 && y < bandY1) {
                    drawDashedHLine(b, x0, bodyLineEndX(offset, y), y - bandY0);
                }
                y += 1 + kAllDayTimedSepGapAfterPx;
            }

            if (!clippedBand) {
                for (int i = 0; i < day.count; i++) {
                    if (eventIsAllDay(day.items[i])) {
                        continue;
                    }
                    if (y + eventBlockH > maxY) {
                        clippedBand = true;
                        break;
                    }
                    const CalendarEvent& ev = day.items[i];
                    if (y + eventBlockH > bandY0 && y < bandY1) {
                        drawMetaTimeAndCal(b, red, x0, y - bandY0, ev, metaColMaxW);
                        drawEventTitle(b, titleColX, y - bandY0, ev.title,
                                       maxTitleWidthPx(offset, y, titleColX),
                                       eventTitleDrawSize(textSz));
                    }
                    y += rowH + kBelowEventTitleGapPx;
                    eventsDrawnBand++;
                }
            }
            const int omittedBand = day.count - eventsDrawnBand;
            drawOverflowEventsFooter(b, offset, y, maxY, omittedBand, textSz, bodyLeftPad, rowStep,
                                     bandY0, bandH);
        }
        y += kBetweenDaySectionsGapPx;
    }
    drawRedScaleLogoClipped(b, red, bandY0, bandH);
}

}  // namespace

void drawCalendarViewBanded(Epd& epd, uint8_t* bandBlack, uint8_t* bandRed, size_t bandBufBytes,
                            int bandH, const DayEvents threeDays[3], bool hadFetch) {
    if (!bandBlack || !bandRed || bandH <= 0) {
        return;
    }
    const size_t need = (static_cast<size_t>(W) / 8U) * static_cast<size_t>(bandH);
    if (bandBufBytes < need) {
        return;
    }

    uint8_t measBlack[800];
    uint8_t measRed[800];
    LocalCanvas measB(W, 8, measBlack);
    LocalCanvas measR(W, 8, measRed);

    const uint8_t textSz = pickSectionTextSize(measB, measR, threeDays, hadFetch);

    epd.SendCommand(0x10);
    for (int bandY0 = 0; bandY0 < H; bandY0 += bandH) {
        const int bh = (bandY0 + bandH > H) ? (H - bandY0) : bandH;
        LocalCanvas cb(W, bh, bandBlack);
        LocalCanvas cr(W, bh, bandRed);
        cb.fillScreen(1);
        cr.fillScreen(1);
        drawEventsBand(cb, cr, threeDays, hadFetch, textSz, bandY0, bh);
        const size_t n = (static_cast<size_t>(W) / 8U) * static_cast<size_t>(bh);
        for (size_t i = 0; i < n; i++) {
            epd.SendData(bandBlack[i]);
        }
    }

    epd.SendCommand(0x13);
    for (int bandY0 = 0; bandY0 < H; bandY0 += bandH) {
        const int bh = (bandY0 + bandH > H) ? (H - bandY0) : bandH;
        LocalCanvas cb(W, bh, bandBlack);
        LocalCanvas cr(W, bh, bandRed);
        cb.fillScreen(1);
        cr.fillScreen(1);
        drawEventsBand(cb, cr, threeDays, hadFetch, textSz, bandY0, bh);
        const size_t n = (static_cast<size_t>(W) / 8U) * static_cast<size_t>(bh);
        for (size_t i = 0; i < n; i++) {
            epd.SendData(static_cast<uint8_t>(bandRed[i] ^ 0xFF));
        }
    }
    epd.SendCommand(0x12);
    delay(100);
    epd.WaitUntilIdle();
}

void drawCalendarView(GFXcanvas1& canvasBlack, GFXcanvas1& canvasRed, const DayEvents threeDays[3],
                      bool hadFetch) {
    if (!canvasBlack.getBuffer() || !canvasRed.getBuffer()) {
        return;
    }

    canvasBlack.fillScreen(1);
    canvasRed.fillScreen(1);

    drawEvents(canvasBlack, canvasRed, threeDays, hadFetch);
}
