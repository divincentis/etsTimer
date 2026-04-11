#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <RTClib.h>

#define SCHEDULE_FILE "/schedule.json"

// Override zone selector
enum OverrideZone {
    ZONE_RL1    = 0,
    ZONE_RL23   = 1,
    ZONE_RL789  = 2
};

struct TimeWindow {
    uint8_t startHour;
    uint8_t startMinute;
    uint8_t endHour;
    uint8_t endMinute;
};

struct Schedule {
    TimeWindow monSat;          // Peak window Mon–Sat
    bool sundayAlwaysOffPeak;   // If true, Sunday is never peak
    // Timezone handled by POSIX_TZ_STRING in config.h — not stored here
};

class ScheduleManager {
public:
    Schedule current;

    void begin() {
        setDefaults();
        load();
    }

    void setDefaults() {
        current.monSat             = { 17, 0, 22, 0 };  // 5:00 PM – 10:00 PM
        current.sundayAlwaysOffPeak = true;
    }

    bool load() {
        if (!LittleFS.exists(SCHEDULE_FILE)) {
            Serial.println("[Schedule] No schedule file, using defaults");
            return false;
        }

        File f = LittleFS.open(SCHEDULE_FILE, "r");
        if (!f) return false;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();

        if (err) {
            Serial.printf("[Schedule] JSON parse error: %s\n", err.c_str());
            return false;
        }

        current.monSat.startHour    = doc["monSat"]["startHour"]   | 17;
        current.monSat.startMinute  = doc["monSat"]["startMinute"] | 0;
        current.monSat.endHour      = doc["monSat"]["endHour"]     | 22;
        current.monSat.endMinute    = doc["monSat"]["endMinute"]   | 0;
        current.sundayAlwaysOffPeak = doc["sundayAlwaysOffPeak"]   | true;

        Serial.println("[Schedule] Loaded from LittleFS");
        return true;
    }

    bool save() {
        File f = LittleFS.open(SCHEDULE_FILE, "w");
        if (!f) {
            Serial.println("[Schedule] Failed to open schedule file for write");
            return false;
        }

        JsonDocument doc;
        doc["monSat"]["startHour"]   = current.monSat.startHour;
        doc["monSat"]["startMinute"] = current.monSat.startMinute;
        doc["monSat"]["endHour"]     = current.monSat.endHour;
        doc["monSat"]["endMinute"]   = current.monSat.endMinute;
        doc["sundayAlwaysOffPeak"]   = current.sundayAlwaysOffPeak;

        serializeJson(doc, f);
        f.close();
        Serial.println("[Schedule] Saved to LittleFS");
        return true;
    }

    // Returns true if supplied local time is in peak period
    bool isPeak(const DateTime& localNow) {
        uint8_t dow = localNow.dayOfTheWeek(); // 0=Sunday

        if (dow == 0 && current.sundayAlwaysOffPeak) {
            return false;
        }

        uint16_t currentMins = localNow.hour() * 60 + localNow.minute();
        uint16_t startMins   = current.monSat.startHour * 60 + current.monSat.startMinute;
        uint16_t endMins     = current.monSat.endHour   * 60 + current.monSat.endMinute;

        // Handle window that crosses midnight if ever needed
        if (startMins <= endMins) {
            return (currentMins >= startMins && currentMins < endMins);
        } else {
            return (currentMins >= startMins || currentMins < endMins);
        }
    }

    // Serialize to JSON for web API
    String toJson() {
        JsonDocument doc;
        doc["monSat"]["startHour"]   = current.monSat.startHour;
        doc["monSat"]["startMinute"] = current.monSat.startMinute;
        doc["monSat"]["endHour"]     = current.monSat.endHour;
        doc["monSat"]["endMinute"]   = current.monSat.endMinute;
        doc["sundayAlwaysOffPeak"]   = current.sundayAlwaysOffPeak;

        String out;
        serializeJson(doc, out);
        return out;
    }
};
