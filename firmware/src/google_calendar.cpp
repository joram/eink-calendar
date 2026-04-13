#include "google_calendar.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "google_secrets.h"

namespace {

/** OAuth + calendar list (small JSON). */
constexpr uint16_t kGoogleHttpTimeoutMs = 45000;
/**
 * Events list: large calendars + singleEvents can return huge JSON — small pages + long read
 * timeout avoid HTTPC_ERROR_READ_TIMEOUT (-11). HTTPClient::setTimeout is uint16_t (max ~65s).
 */
constexpr uint16_t kGoogleEventsReadTimeoutMs = 65000;
/** Smaller pages transfer faster; nextPageToken fetches the rest. */
constexpr int kEventsMaxResultsPerPage = 25;
/**
 * Partial response: summary + start per Google Calendar EventDateTime (dateTime is RFC3339 with
 * offset or Z; optional timeZone is IANA — calendars can use different default zones).
 * @see https://developers.google.com/calendar/api/v3/reference/events#resource
 */
static const char kEventsFieldsPartial[] = "items(summary,start(date,dateTime,timeZone)),nextPageToken";
/** Max title length stored (UTF-8 safe truncation via strlcpy). */
constexpr size_t kMaxTitleStored = 32;
/** mbedTLS handshake can be slow on congested WiFi. */
constexpr uint32_t kTlsHandshakeTimeoutSec = 45;

/** Per-day raw slots before flush to UI (sorted, then capped to kMaxDisplayPerDay). */
constexpr int kMaxRaw = 96;

struct RawEv {
    time_t sort;
    char timeStr[20];
    char calAbbrev[16];
    char title[kMaxTitleStored + 1];
};

RawEv g_raw[3][kMaxRaw];
int g_rawCount[3];

void clearDays(DayEvents days[3]) {
    for (int i = 0; i < 3; i++) {
        days[i].count = 0;
    }
}

void dayStrFromOffset(int dayOffset, char out[11]) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    lt.tm_hour = 0;
    lt.tm_min = 0;
    lt.tm_sec = 0;
    lt.tm_mday += dayOffset;
    mktime(&lt);
    strftime(out, 11, "%Y-%m-%d", &lt);
}

/** All-day events: compare YYYY-MM-DD to the next three local civil dates. */
int dayIndexForIsoDatePrefix(const char* iso) {
    if (!iso || strlen(iso) < 10) {
        return -1;
    }
    char d0[11], d1[11], d2[11];
    dayStrFromOffset(0, d0);
    dayStrFromOffset(1, d1);
    dayStrFromOffset(2, d2);
    char prefix[11];
    memcpy(prefix, iso, 10);
    prefix[10] = 0;
    if (strcmp(prefix, d0) == 0) {
        return 0;
    }
    if (strcmp(prefix, d1) == 0) {
        return 1;
    }
    if (strcmp(prefix, d2) == 0) {
        return 2;
    }
    return -1;
}

/** Pointer to timezone designator after seconds (…SS or …SS.fff), or nullptr. */
static const char* rfc3339AfterSeconds(const char* s) {
    const char* p = strchr(s, 'T');
    if (!p) {
        return nullptr;
    }
    p++;
    p = strchr(p, ':');
    if (!p) {
        return nullptr;
    }
    p++;
    p = strchr(p, ':');
    if (!p) {
        return nullptr;
    }
    p++;
    while (isdigit((unsigned char)*p)) {
        p++;
    }
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p)) {
            p++;
        }
    }
    return p;
}

static bool isLeapYear(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/** Days in month (1–12), year y Gregorian. */
static int daysInMonth(int y, int month) {
    static const uint8_t d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 31;
    }
    if (month == 2 && isLeapYear(y)) {
        return 29;
    }
    return d[month - 1];
}

/**
 * UTC civil time → seconds since Unix epoch (no libc TZ; avoids setenv during JSON parse racing
 * localtime_r used for display).
 */
static time_t utcCivilToEpoch(int y, int mo, int d, int h, int mi, int se) {
    int64_t days = 0;
    for (int yy = 1970; yy < y; yy++) {
        days += isLeapYear(yy) ? 366 : 365;
    }
    for (int m = 1; m < mo; m++) {
        days += daysInMonth(y, m);
    }
    days += (int64_t)(d - 1);
    return (time_t)(days * 86400LL + (int64_t)h * 3600LL + (int64_t)mi * 60LL + (int64_t)se);
}

/**
 * Parse RFC3339 start.dateTime to a UTC instant. Handles Z and numeric offsets; ignores fractional
 * seconds. (EventDateTime uses RFC3339; calendars may emit Z or a fixed offset per calendar zone.)
 */
static bool rfc3339DateTimeToUtc(const char* s, time_t* outUtc) {
    if (!s || !outUtc) {
        return false;
    }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 6) {
        return false;
    }
    const char* z = rfc3339AfterSeconds(s);
    if (!z || !*z) {
        return false;
    }
    int offSec = 0;
    if (*z == 'Z' || *z == 'z') {
        offSec = 0;
    } else if (*z == '+' || *z == '-') {
        int sign = (*z == '-') ? -1 : 1;
        int oh = 0, om = 0;
        if (sscanf(z + 1, "%d:%d", &oh, &om) < 1) {
            return false;
        }
        offSec = sign * (oh * 3600 + om * 60);
    } else {
        return false;
    }

    const int64_t naiveAsUtc = (int64_t)utcCivilToEpoch(y, mo, d, h, mi, se);
    *outUtc = (time_t)(naiveAsUtc - (int64_t)offSec);
    return true;
}

static void formatTime12hFromLocalInstant(time_t utc, char* out, size_t outLen) {
    struct tm lt;
    localtime_r(&utc, &lt);
    int h = lt.tm_hour;
    int mi = lt.tm_min;
    bool pm = h >= 12;
    int h12 = h % 12;
    if (h12 == 0) {
        h12 = 12;
    }
    snprintf(out, outLen, "%d:%02d %s", h12, mi, pm ? "PM" : "AM");
}

/** Which of the next three local civil days contains this instant (timed events). */
static int dayIndexForLocalInstant(time_t utc) {
    struct tm lt;
    localtime_r(&utc, &lt);
    char evDay[11];
    strftime(evDay, sizeof(evDay), "%Y-%m-%d", &lt);
    char d0[11], d1[11], d2[11];
    dayStrFromOffset(0, d0);
    dayStrFromOffset(1, d1);
    dayStrFromOffset(2, d2);
    if (strcmp(evDay, d0) == 0) {
        return 0;
    }
    if (strcmp(evDay, d1) == 0) {
        return 1;
    }
    if (strcmp(evDay, d2) == 0) {
        return 2;
    }
    return -1;
}

void rfc3339UtcFromLocalMidnight(int dayOffsetFromToday, char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    lt.tm_hour = 0;
    lt.tm_min = 0;
    lt.tm_sec = 0;
    lt.tm_mday += dayOffsetFromToday;
    time_t t = mktime(&lt);
    struct tm utc;
    gmtime_r(&t, &utc);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &utc);
}

/** application/x-www-form-urlencoded (values often contain /, +, etc.) */
static void formValueAppend(String& out, const char* s) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char hex[5];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        }
    }
}

bool refreshAccessToken(String& token) {
    if (!GOOGLE_REFRESH_TOKEN[0] || !GOOGLE_CLIENT_ID[0]) {
        return false;
    }
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(kTlsHandshakeTimeoutSec);
    HTTPClient http;
    if (!http.begin(client, "https://oauth2.googleapis.com/token")) {
        return false;
    }
    http.setTimeout(kGoogleHttpTimeoutMs);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String body = "grant_type=refresh_token&client_id=";
    formValueAppend(body, GOOGLE_CLIENT_ID);
    body += "&client_secret=";
    formValueAppend(body, GOOGLE_CLIENT_SECRET);
    body += "&refresh_token=";
    formValueAppend(body, GOOGLE_REFRESH_TOKEN);
    int code = http.POST(body);
    String resp = http.getString();
    http.end();
    if (code != 200) {
        Serial.printf("OAuth HTTP %d: %s (WiFi=%d heap=%u)\n", code, resp.c_str(),
                      static_cast<int>(WiFi.status()), static_cast<unsigned>(ESP.getFreeHeap()));
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        return false;
    }
    const char* at = doc["access_token"];
    if (!at) {
        return false;
    }
    token = at;
    return true;
}

void urlEncodePath(const char* in, char* out, size_t cap) {
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && o + 4 < cap; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            o += snprintf(out + o, cap - o, "%%%02X", c);
        }
    }
    out[o] = 0;
}

int cmpRaw(const void* a, const void* b) {
    const RawEv* ra = static_cast<const RawEv*>(a);
    const RawEv* rb = static_cast<const RawEv*>(b);
    if (ra->sort < rb->sort) {
        return -1;
    }
    if (ra->sort > rb->sort) {
        return 1;
    }
    return strcmp(ra->title, rb->title);
}

/**
 * Short label for multi-calendar prefixes: "Caitlin Blank" -> "CB"; "john@oram.ca" -> "j".
 * Email-shaped names use the first alphanumeric of the local part (lowercase); otherwise
 * first letter of each whitespace-separated word (uppercase).
 */
void abbreviateCalendarName(const char* name, char* out, size_t cap) {
    out[0] = 0;
    if (!name || !name[0] || cap < 2) {
        return;
    }
    const char* at = strchr(name, '@');
    if (at) {
        for (const char* p = name; p < at; p++) {
            if (isalnum((unsigned char)*p)) {
                out[0] = (char)tolower((unsigned char)*p);
                out[1] = 0;
                return;
            }
        }
        return;
    }
    size_t o = 0;
    for (const char* p = name; *p && o + 1 < cap;) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        if (isalnum((unsigned char)*p)) {
            out[o++] = (char)toupper((unsigned char)*p);
        }
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
    }
    out[o] = 0;
}

void addEvent(int dayIdx, const char* timeStr, const char* title, time_t sortKey, bool multiCal,
              const char* calName) {
    if (dayIdx < 0 || dayIdx > 2 || g_rawCount[dayIdx] >= kMaxRaw) {
        static bool warnedFull;
        if (!warnedFull) {
            warnedFull = true;
            Serial.println("Calendar: per-day raw buffer full; increase kMaxRaw or reduce calendars");
        }
        return;
    }
    RawEv& r = g_raw[dayIdx][g_rawCount[dayIdx]];
    r.sort = sortKey;
    strlcpy(r.timeStr, timeStr, sizeof(r.timeStr));
    r.calAbbrev[0] = 0;
    if (multiCal && calName && calName[0]) {
        abbreviateCalendarName(calName, r.calAbbrev, sizeof(r.calAbbrev));
    }
    strlcpy(r.title, title, sizeof(r.title));
    g_rawCount[dayIdx]++;
}

/** Copy event title at ingest: max @ref kMaxTitleStored chars (+ NUL). */
static void copyTitleTruncated(const char* summary, char out[kMaxTitleStored + 1]) {
    const char* s = summary && summary[0] ? summary : "(No title)";
    strlcpy(out, s, kMaxTitleStored + 1);
}

/** Add events from one calendar response; @p todayOnly: only day 0, else only days 1–2. */
static void ingestEventArray(JsonArray evItems, bool multiCal, const char* calSum, bool todayOnly) {
    char titleBuf[kMaxTitleStored + 1];
    for (JsonObject ev : evItems) {
        const char* summ = ev["summary"];
        copyTitleTruncated(summ, titleBuf);

        JsonObject start = ev["start"];
        const char* dtime = start["dateTime"];
        const char* ddate = start["date"];

        char timeLabel[20];
        const char* isoForDay = nullptr;

        time_t sortUtc = 0;
        if (dtime) {
            if (!rfc3339DateTimeToUtc(dtime, &sortUtc)) {
                continue;
            }
            formatTime12hFromLocalInstant(sortUtc, timeLabel, sizeof(timeLabel));
        } else if (ddate) {
            strlcpy(timeLabel, "All day", sizeof(timeLabel));
            isoForDay = ddate;
        } else {
            continue;
        }

        int di = -1;
        if (dtime) {
            di = dayIndexForLocalInstant(sortUtc);
        } else {
            di = dayIndexForIsoDatePrefix(isoForDay);
        }
        if (di < 0) {
            continue;
        }
        if (todayOnly) {
            if (di != 0) {
                continue;
            }
        } else if (di != 1 && di != 2) {
            continue;
        }

        time_t sk = dtime ? sortUtc : 0;
        addEvent(di, timeLabel, titleBuf, sk, multiCal, calSum);
    }
}

static void fetchAndIngestOneCalendar(JsonObject cal, const String& accessToken, const char* encTMin,
                                      const char* encTMax, bool multiCal, bool todayOnly) {
    const char* calId = cal["id"];
    const char* calSum = cal["summary"].as<const char*>();
    if (!calSum) {
        calSum = "Calendar";
    }
    if (!calId) {
        return;
    }

    char enc[256];
    urlEncodePath(calId, enc, sizeof(enc));

    char authHdr[512];
    snprintf(authHdr, sizeof(authHdr), "Bearer %s", accessToken.c_str());

    String pageToken;
    for (int page = 0; page < 20; page++) {
        String evUrl = "https://www.googleapis.com/calendar/v3/calendars/";
        evUrl += enc;
        evUrl += "/events?singleEvents=true&orderBy=startTime&maxResults=";
        evUrl += kEventsMaxResultsPerPage;
        evUrl += "&timeMin=";
        evUrl += encTMin;
        evUrl += "&timeMax=";
        evUrl += encTMax;
        evUrl += "&fields=";
        formValueAppend(evUrl, kEventsFieldsPartial);
        if (pageToken.length() > 0) {
            evUrl += "&pageToken=";
            formValueAppend(evUrl, pageToken.c_str());
        }

        bool gotPage = false;
        for (int attempt = 0; attempt < 8; attempt++) {
            if (attempt > 0) {
                delay(50U + 50U * static_cast<unsigned>(attempt));
            }
            WiFiClientSecure clientEv;
            clientEv.setInsecure();
            clientEv.setHandshakeTimeout(kTlsHandshakeTimeoutSec);
            HTTPClient httpEv;
            if (!httpEv.begin(clientEv, evUrl)) {
                Serial.printf("Calendar events begin() failed (%s) attempt %d\n", calSum, attempt + 1);
                continue;
            }
            httpEv.setTimeout(kGoogleEventsReadTimeoutMs);
            httpEv.addHeader("Authorization", authHdr);
            httpEv.addHeader("Accept-Encoding", "identity");
            int evCode = httpEv.GET();
            if (evCode != 200) {
                String errDetail = httpEv.errorToString(evCode);
                httpEv.end();
                if (evCode > 0) {
                    Serial.printf("Calendar events HTTP %d (%s)\n", evCode, calSum);
                    return;
                }
                Serial.printf("Calendar events HTTP %d (%s) %s attempt %d\n", evCode, calSum,
                              errDetail.c_str(), attempt + 1);
                continue;
            }
            String evBody = httpEv.getString();
            httpEv.end();
            if (evBody.length() == 0) {
                Serial.printf("Calendar events empty body (%s) attempt %d\n", calSum, attempt + 1);
                continue;
            }

            JsonDocument evDoc;
            DeserializationError jerr = deserializeJson(evDoc, evBody);
            if (jerr == DeserializationError::IncompleteInput) {
                Serial.printf("Calendar events JSON (%s): IncompleteInput len=%u — refetch\n", calSum,
                              static_cast<unsigned>(evBody.length()));
                continue;
            }
            if (jerr) {
                Serial.printf("Calendar events JSON (%s): %s\n", calSum, jerr.c_str());
                return;
            }
            JsonArray evItems = evDoc["items"].as<JsonArray>();
            if (!evItems.isNull()) {
                ingestEventArray(evItems, multiCal, calSum, todayOnly);
            }

            if (evDoc["nextPageToken"].isNull()) {
                gotPage = true;
                pageToken = String();
                break;
            }
            const char* npt = evDoc["nextPageToken"].as<const char*>();
            if (!npt || !npt[0]) {
                gotPage = true;
                pageToken = String();
                break;
            }
            pageToken = npt;
            gotPage = true;
            delay(50);
            break;
        }
        if (!gotPage) {
            return;
        }
        if (pageToken.length() == 0) {
            break;
        }
    }
}

constexpr int kMaxDisplayPerDay = 24;

void flushRawToDays(DayEvents days[3]) {
    for (int d = 0; d < 3; d++) {
        qsort(g_raw[d], g_rawCount[d], sizeof(RawEv), cmpRaw);
        int n = g_rawCount[d];
        if (n > kMaxDisplayPerDay) {
            n = kMaxDisplayPerDay;
        }
        days[d].count = n;
        for (int i = 0; i < n; i++) {
            strlcpy(days[d].items[i].timeStr, g_raw[d][i].timeStr, sizeof(days[d].items[i].timeStr));
            strlcpy(days[d].items[i].calAbbrev, g_raw[d][i].calAbbrev, sizeof(days[d].items[i].calAbbrev));
            strlcpy(days[d].items[i].title, g_raw[d][i].title, sizeof(days[d].items[i].title));
        }
    }
}

}  // namespace

bool fetchGoogleCalendarThreeDays(DayEvents days[3]) {
    clearDays(days);
    for (int i = 0; i < 3; i++) {
        g_rawCount[i] = 0;
    }

    if (WiFi.status() != WL_CONNECTED || !GOOGLE_REFRESH_TOKEN[0]) {
        Serial.println("Calendar: no WiFi or google_secrets.h not configured");
        return false;
    }

    String accessToken;
    if (!refreshAccessToken(accessToken)) {
        Serial.println("Calendar: OAuth failed");
        return false;
    }

    char tMin[40], tMax[40];
    rfc3339UtcFromLocalMidnight(0, tMin, sizeof(tMin));
    rfc3339UtcFromLocalMidnight(3, tMax, sizeof(tMax));
    char encMin[80], encMax[80];
    urlEncodePath(tMin, encMin, sizeof(encMin));
    urlEncodePath(tMax, encMax, sizeof(encMax));

    // --- calendar list ---
    WiFiClientSecure clientList;
    clientList.setInsecure();
    clientList.setHandshakeTimeout(kTlsHandshakeTimeoutSec);
    String listUrl =
        "https://www.googleapis.com/calendar/v3/users/me/calendarList?minAccessRole=reader&maxResults=250";
    HTTPClient httpList;
    if (!httpList.begin(clientList, listUrl)) {
        return false;
    }
    httpList.setTimeout(kGoogleHttpTimeoutMs);
    char authHdr[512];
    snprintf(authHdr, sizeof(authHdr), "Bearer %s", accessToken.c_str());
    httpList.addHeader("Authorization", authHdr);
    httpList.addHeader("Accept-Encoding", "identity");
    int listCode = httpList.GET();
    if (listCode != 200) {
        Serial.printf("calendarList HTTP %d\n", listCode);
        httpList.end();
        return false;
    }

    String listBody = httpList.getString();
    httpList.end();
    if (listBody.length() == 0) {
        Serial.println("calendarList: empty body");
        return false;
    }

    JsonDocument listDoc;
    DeserializationError jerr = deserializeJson(listDoc, listBody);
    if (jerr) {
        Serial.printf("calendarList JSON: %s (len=%u)\n", jerr.c_str(),
                      static_cast<unsigned>(listBody.length()));
        return false;
    }
    JsonArray items = listDoc["items"].as<JsonArray>();
    if (items.isNull()) {
        flushRawToDays(days);
        return true;
    }

    int nCals = items.size();
    bool multiCal = nCals > 1;

    // Pass 1: today only — every calendar contributes to day 0 before later days fill raw buffers.
    for (JsonObject cal : items) {
        fetchAndIngestOneCalendar(cal, accessToken, encMin, encMax, multiCal, true);
        delay(50);
    }
    // Pass 2: tomorrow and the day after.
    for (JsonObject cal : items) {
        fetchAndIngestOneCalendar(cal, accessToken, encMin, encMax, multiCal, false);
        delay(50);
    }

    flushRawToDays(days);
    Serial.println("Calendar: fetch ok");
    return true;
}
