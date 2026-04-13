/**
 * Calendar UI on 7.5" B/W/R (V2), Waveshare E-Paper ESP32 Driver Board.
 * Google Calendar fetch runs on a dedicated task — TLS + JSON exceed the default loop() stack.
 */

#include <Arduino.h>
#include <cstdlib>
#include <esp_sleep.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "calendar_view.h"
#include "clock_sync.h"
#include "epd7in5b_V2.h"
#include "google_calendar.h"
#include "google_secrets.h"

static constexpr uint32_t EPD_W = 800;
static constexpr uint32_t EPD_H = 480;
/** Horizontal band height for render buffers (~16 KiB/plane; fits fragmented heap after TLS). */
static constexpr int kBandH = 160;
static constexpr size_t kBandBytes = (EPD_W / 8) * kBandH;

static uint8_t* s_bandBlack = nullptr;
static uint8_t* s_bandRed = nullptr;

static void freeBandBuffers() {
    free(s_bandBlack);
    free(s_bandRed);
    s_bandBlack = nullptr;
    s_bandRed = nullptr;
}

/** Dedicated stack for HTTPS + ArduinoJson (bytes — matches Arduino loopTask convention). */
static constexpr uint32_t kCalFetchStackBytes = 32768;

/** Deep sleep between updates (ESP32 timer wake). */
static constexpr uint64_t kDeepSleepIntervalUs = 3600ULL * 1000000ULL;

static void deepSleepOneHour() {
    Serial.printf("Deep sleep %llu s\n", kDeepSleepIntervalUs / 1000000ULL);
    Serial.flush();
    delay(200);
    esp_sleep_enable_timer_wakeup(kDeepSleepIntervalUs);
    esp_deep_sleep_start();
}

static Epd epd;

static DayEvents s_calDays[3];
static bool s_calFetchOk = false;
static SemaphoreHandle_t s_calDone;

static void calFetchTask(void* /*param*/) {
    s_calFetchOk = fetchGoogleCalendarThreeDays(s_calDays);
    xSemaphoreGive(s_calDone);
    vTaskDelete(nullptr);
}

/** Tear down WiFi/TCP to reclaim lwIP buffers before pushing the frame. */
static void releaseNetworkForDisplay() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("eink-calendar ESP32");
    {
        esp_sleep_wakeup_cause_t w = esp_sleep_get_wakeup_cause();
        if (w == ESP_SLEEP_WAKEUP_TIMER) {
            Serial.println("Wake: timer");
        } else if (w != ESP_SLEEP_WAKEUP_UNDEFINED) {
            Serial.printf("Wake: cause=%d\n", static_cast<int>(w));
        }
    }

    clockSync();

    bool calOk = false;
    if (WiFi.status() == WL_CONNECTED && GOOGLE_REFRESH_TOKEN[0]) {
        s_calDone = xSemaphoreCreateBinary();
        if (s_calDone) {
            BaseType_t created =
                xTaskCreate(calFetchTask, "calFetch", kCalFetchStackBytes, nullptr, 5, nullptr);
            if (created == pdPASS) {
                xSemaphoreTake(s_calDone, portMAX_DELAY);
                calOk = s_calFetchOk;
                if (calOk) {
                    int n = s_calDays[0].count + s_calDays[1].count + s_calDays[2].count;
                    Serial.printf("Calendar: loaded %d events (3 days)\n", n);
                }
            } else {
                Serial.println("Calendar: xTaskCreate failed");
            }
            vSemaphoreDelete(s_calDone);
            s_calDone = nullptr;
        }
    } else {
        Serial.println("Calendar: need WiFi (wifi_secrets.h) + GOOGLE_REFRESH_TOKEN");
    }

    releaseNetworkForDisplay();
    delay(150);

    s_bandBlack = static_cast<uint8_t*>(malloc(kBandBytes));
    s_bandRed = static_cast<uint8_t*>(malloc(kBandBytes));
    if (!s_bandBlack || !s_bandRed) {
        freeBandBuffers();
        Serial.printf("EPD band malloc failed (heap=%u maxAlloc=%u)\n", ESP.getFreeHeap(),
                      ESP.getMaxAllocHeap());
        while (true) {
            delay(1000);
        }
    }

    if (epd.Init() != 0) {
        freeBandBuffers();
        Serial.println("e-Paper init failed");
        while (true) {
            delay(1000);
        }
    }

    drawCalendarViewBanded(epd, s_bandBlack, s_bandRed, kBandBytes, kBandH, s_calDays, calOk);

    freeBandBuffers();

    epd.Sleep();
    Serial.println("Display updated");

    deepSleepOneHour();
}

void loop() {
    delay(1000);
}
