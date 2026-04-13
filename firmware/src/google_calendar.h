#pragma once

#include <stddef.h>

/** One agenda item: time + optional calendar abbrev on a small line; title on the line below. */
struct CalendarEvent {
    char timeStr[20];
    /** Set when multiple calendars are shown; empty otherwise. */
    char calAbbrev[16];
    /** Ingest truncates API summary to 32 chars (+ NUL) to save RAM. */
    char title[33];
};

/** Events for a single local calendar day (next 3 days: today, tomorrow, day after). */
struct DayEvents {
    int count;
    CalendarEvent items[24];
};

/**
 * Fetches all accessible calendars for the OAuth account (e.g. john@oram.ca) and fills
 * `days[0..2]` for the next three local days. Requires WiFi + valid google_secrets.h.
 * Returns true if at least the token request succeeded; leaves days empty on total failure.
 */
bool fetchGoogleCalendarThreeDays(DayEvents days[3]);
