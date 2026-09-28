#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "EventLog.h"
#include "ScheduleJson.h"
#include "WeekSchedule.h"

// Persists the weekly schedule and the setback flag in NVS (the ESP32's
// wear-levelled key/value flash store), so there is no filesystem image to
// upload and settings survive firmware updates.
class ScheduleManager {
public:
    ets::WeekSchedule schedule = ets::WeekSchedule::defaults();
    bool              setback  = false;

    void begin() {
        _prefs.begin(NVS_NAMESPACE, false);

        String json = _prefs.getString(KEY_SCHEDULE, "");
        if (json.length() == 0) {
            eventLog.add("No saved schedule - using defaults (Mon-Sat 17:00-22:00)");
        } else {
            JsonDocument doc;
            ets::WeekSchedule loaded;
            const char* err = deserializeJson(doc, json) ? "corrupt JSON"
                            : ets::scheduleFromJson(doc.as<JsonVariantConst>(), loaded);
            if (err) {
                eventLog.add("Saved schedule invalid (%s) - using defaults", err);
            } else {
                schedule = loaded;
            }
        }

        setback = _prefs.getBool(KEY_SETBACK, false);
    }

    void saveSchedule(const ets::WeekSchedule& s) {
        schedule = s;
        JsonDocument doc;
        ets::scheduleToJson(s, doc.to<JsonObject>());
        String json;
        serializeJson(doc, json);
        _prefs.putString(KEY_SCHEDULE, json);
    }

    void saveSetback(bool on) {
        setback = on;
        _prefs.putBool(KEY_SETBACK, on);
    }

private:
    static constexpr const char* NVS_NAMESPACE = "etstimer";
    static constexpr const char* KEY_SCHEDULE  = "schedule";
    static constexpr const char* KEY_SETBACK   = "setback";

    Preferences _prefs;
};
