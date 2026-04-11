#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <time.h>
#include "config.h"

class TimeManager {
public:
    RTC_DS3231 rtc;
    bool rtcOk     = false;
    bool ntpSynced = false;
    unsigned long lastNtpSync = 0;

    bool begin() {
        Wire.begin(PIN_SDA, PIN_SCL);
        rtcOk = rtc.begin();

        if (!rtcOk) {
            Serial.println("[Time] DS3231 not found! Timekeeping will be unreliable.");
            return false;
        }

        // Set POSIX TZ immediately so local time is correct even before NTP
        setenv("TZ", POSIX_TZ_STRING, 1);
        tzset();

        if (rtc.lostPower()) {
            Serial.println("[Time] RTC lost power — time not set. NTP sync required.");
        } else {
            Serial.println("[Time] RTC OK");
            DateTime n = nowLocal();
            Serial.printf("[Time] Local time: %04d-%02d-%02d %02d:%02d:%02d %s\n",
                n.year(), n.month(), n.day(),
                n.hour(), n.minute(), n.second(),
                tzAbbr().c_str());
        }

        return true;
    }

    // Call after WiFi connects.
    // NTP always returns UTC. POSIX_TZ_STRING handles local time + DST.
    // RTC is always stored in UTC — local time derived at read time.
    bool syncNTP() {
        Serial.println("[Time] Starting NTP sync...");

        // POSIX TZ already set in begin() — just fetch UTC from NTP
        configTime(0, 0, NTP_SERVER);

        struct tm timeinfo;
        int attempts = 0;
        while (!getLocalTime(&timeinfo) && attempts < 20) {
            delay(500);
            attempts++;
            Serial.print(".");
        }
        Serial.println();

        if (attempts >= 20) {
            Serial.println("[Time] NTP sync failed, falling back to RTC");
            return false;
        }

        // Store UTC in RTC — never store local time to avoid DST double-adjustment
        time_t utcNow = time(nullptr);
        if (rtcOk) {
            rtc.adjust(DateTime((uint32_t)utcNow));
            Serial.println("[Time] RTC updated from NTP (UTC stored)");
        }

        ntpSynced = true;
        lastNtpSync = millis();
        Serial.printf("[Time] NTP sync OK. Local: %s", asctime(&timeinfo));
        return true;
    }

    // Periodic re-sync — TZ env already set, just refresh UTC from NTP
    void loop() {
        if (ntpSynced && (millis() - lastNtpSync > NTP_SYNC_INTERVAL_MS)) {
            Serial.println("[Time] Periodic NTP re-sync");
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                time_t utcNow = time(nullptr);
                if (rtcOk) {
                    rtc.adjust(DateTime((uint32_t)utcNow));
                }
                lastNtpSync = millis();
                Serial.println("[Time] Periodic NTP sync OK");
            }
        }
    }

    // Returns local time, DST-correct via POSIX TZ string
    // RTC stores UTC; this converts to local automatically
    DateTime nowLocal() {
        time_t utc;
        if (rtcOk) {
            utc = (time_t)rtc.now().unixtime();
        } else {
            utc = time(nullptr);
        }
        struct tm local;
        localtime_r(&utc, &local);
        return DateTime(
            local.tm_year + 1900,
            local.tm_mon  + 1,
            local.tm_mday,
            local.tm_hour,
            local.tm_min,
            local.tm_sec
        );
    }

    // Formatted local time string for UI
    String timeString() {
        DateTime n = nowLocal();
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
            n.year(), n.month(), n.day(),
            n.hour(), n.minute(), n.second());
        return String(buf);
    }

    // Current TZ abbreviation — "MST" or "MDT"
    String tzAbbr() {
        time_t utc = time(nullptr);
        struct tm local;
        localtime_r(&utc, &local);
        return String(local.tm_zone);
    }

    // Day of week string
    String dowString() {
        DateTime n = nowLocal();
        const char* days[] = {
            "Sunday","Monday","Tuesday","Wednesday",
            "Thursday","Friday","Saturday"
        };
        return String(days[n.dayOfTheWeek()]);
    }

    float rtcTemperature() {
        if (!rtcOk) return 0.0f;
        return rtc.getTemperature();
    }
};
