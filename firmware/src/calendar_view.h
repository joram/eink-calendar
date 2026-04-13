#pragma once

#include <Adafruit_GFX.h>

#include "google_calendar.h"

/**
 * @param threeDays  events for today, tomorrow, and the following day (from Google when configured)
 * @param hadFetch   true if the HTTP/API path ran successfully (may still be zero events)
 */
void drawCalendarView(GFXcanvas1& canvasBlack, GFXcanvas1& canvasRed, const DayEvents threeDays[3],
                      bool hadFetch);

class Epd;

/**
 * Renders the calendar in horizontal bands and streams pixels to the panel (two small band buffers;
 * avoids ~48 KiB full-frame mallocs that fail when heap is fragmented after TLS).
 */
void drawCalendarViewBanded(Epd& epd, uint8_t* bandBlack, uint8_t* bandRed, size_t bandBufBytes,
                            int bandH, const DayEvents threeDays[3], bool hadFetch);
