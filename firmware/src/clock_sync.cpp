#include "clock_sync.h"

#include <Arduino.h>
#include <cstring>
#include <sys/time.h>

#include "wifi_secrets.h"

#include <WiFi.h>

static int monthFromAbbrev(const char* m) {
    static const char* names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int i = 0; i < 12; i++) {
        if (strncmp(m, names[i], 3) == 0) {
            return i + 1;
        }
    }
    return 1;
}

static bool timeFromBuildMacros(struct tm* out) {
    char mon[4] = {0};
    int day, year;
    int hh, mm, ss;
    if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) != 3) {
        return false;
    }
    if (sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss) != 3) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->tm_year = year - 1900;
    out->tm_mon = monthFromAbbrev(mon) - 1;
    out->tm_mday = day;
    out->tm_hour = hh;
    out->tm_min = mm;
    out->tm_sec = ss;
    return true;
}

static bool syncNtpOverWifi() {
    if (strlen(WIFI_SSID) == 0) {
        return false;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi ");
    for (int i = 0; i < 40; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            break;
        }
        delay(500);
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi failed");
        return false;
    }
    /**
     * ESP32 Arduino: configTime(0,0,...) only syncs UTC; localtime_r often still follows GMT unless
     * the TZ stack is wired via configTzTime (POSIX string + SNTP). Otherwise "today" and event
     * times follow UTC (wrong weekday near midnight; events off by offset e.g. 7h in PDT).
     */
    configTzTime(TZ_POSIX, "pool.ntp.org", "time.nist.gov");
    struct tm tinfo;
    for (int i = 0; i < 60; i++) {
        if (getLocalTime(&tinfo, 0)) {
            Serial.println("NTP ok");
            return true;
        }
        delay(500);
    }
    Serial.println("NTP timeout");
    return false;
}

bool clockSync() {
    if (syncNtpOverWifi()) {
        return true;
    }
    struct tm tm;
    if (!timeFromBuildMacros(&tm)) {
        return false;
    }
    setenv("TZ", TZ_POSIX, 1);
    tzset();
    time_t t = mktime(&tm);
    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
    Serial.println("Clock: build date/time (set wifi_secrets.h + TZ for NTP)");
    return true;
}
