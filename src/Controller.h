#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"
#include "EventLog.h"
#include "RelayController.h"
#include "ScheduleManager.h"
#include "TimeManager.h"

// Owns all device state. loop() runs on the Arduino loop task; the public
// request methods are called from web handlers on the async TCP task.
// A single mutex keeps the two from stepping on each other.
class Controller {
public:
    enum class OverrideResult : uint8_t { Ok, Busy, BadZone };

    // Conditions worth alerting on. No problems = healthy.
    struct Health {
        static constexpr uint8_t MAX = 4;
        const char* problems[MAX] = {};
        uint8_t     count = 0;

        bool ok() const { return count == 0; }
        void add(const char* p) { if (count < MAX) problems[count++] = p; }
    };

    void begin() {
        _lock = xSemaphoreCreateMutex();
        _relays.begin();
        _clock.begin();
        _store.begin();
        _relays.setSetback(_store.setback);
        evaluatePeak();
    }

    void loop() {
        {
            Guard g(_lock);
            _clock.loop();
            _relays.loop();
            if (millis() - _lastEval >= 1000) {
                evaluatePeak();
            }
        }
        if (_rebootAt && (int32_t)(millis() - _rebootAt) >= 0) {
            eventLog.add("Rebooting");
            delay(100);
            ESP.restart();
        }
    }

    void startNtp() {
        Guard g(_lock);
        _clock.startNtp();
    }

    // ── Web requests ────────────────────────────────────────────────────────

    void status(JsonObject out) {
        Guard g(_lock);

        out["device"]   = DEVICE_NAME;
        out["firmware"] = FIRMWARE_VERSION;
        out["uptime"]   = uptimeS();
        out["reset"]    = resetReasonName(esp_reset_reason());

        Health h = healthLocked();
        JsonArray problems = out["problems"].to<JsonArray>();
        for (uint8_t i = 0; i < h.count; i++) problems.add(h.problems[i]);

        struct tm t;
        bool valid = _clock.localNow(t);
        JsonObject time = out["time"].to<JsonObject>();
        time["valid"]   = valid;
        time["source"]  = TimeManager::sourceName(_clock.source());
        time["local"]   = _clock.localString();
        time["tz"]      = _clock.tzAbbr();
        time["dow"]     = t.tm_wday;
        time["minute"]  = t.tm_hour * 60 + t.tm_min;
        time["second"]  = t.tm_sec;
        time["ntpAge"]  = _clock.lastNtpSync() ? (int32_t)(::time(nullptr) - _clock.lastNtpSync()) : -1;

        JsonObject peak = out["peak"].to<JsonObject>();
        peak["active"]  = _relays.get(RELAY_PEAK);
        peak["failsafe"] = !valid;
        peak["minutesUntilChange"] = valid
            ? _store.schedule.minutesUntilChange(t.tm_wday, t.tm_hour * 60 + t.tm_min)
            : -1;

        out["setback"] = _relays.get(RELAY_SETBACK);

        JsonObject ovr = out["override"].to<JsonObject>();
        ovr["busy"]     = _relays.overrideBusy();
        ovr["lastZone"] = _relays.lastOverrideZone();
        JsonArray zones = ovr["zones"].to<JsonArray>();
        for (const char* z : ZONE_LABELS) zones.add(z);

        JsonArray relays = out["relays"].to<JsonArray>();
        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            JsonObject r = relays.add<JsonObject>();
            r["id"]    = RELAYS[i].id;
            r["label"] = RELAYS[i].label;
            r["on"]    = _relays.get((RelayId)i);
        }

        JsonObject rtc = out["rtc"].to<JsonObject>();
        rtc["present"]   = _clock.rtcPresent();
        rtc["lostPower"] = _clock.rtcLostPower();
        if (!isnan(_clock.rtcTempC())) rtc["tempC"] = _clock.rtcTempC();

        JsonObject wifi = out["wifi"].to<JsonObject>();
        wifi["connected"] = WiFi.isConnected();
        wifi["ssid"]      = WiFi.SSID();
        wifi["rssi"]      = WiFi.RSSI();
        wifi["ip"]        = WiFi.localIP().toString();
        wifi["hostname"]  = DEVICE_HOSTNAME;
    }

    Health health() {
        Guard g(_lock);
        return healthLocked();
    }

    // One line for the heartbeat body, e.g.
    // "OFF-PEAK next change 83m | setback off | time ntp MDT | rtc ok 23.5C | wifi -58dBm | up 3h12m | reset power-on | fw 2.1.0"
    String summary() {
        Guard g(_lock);
        struct tm t;
        bool valid = _clock.localNow(t);
        int32_t next = valid ? _store.schedule.minutesUntilChange(t.tm_wday, t.tm_hour * 60 + t.tm_min) : -1;
        uint32_t up = uptimeS();

        char nextBuf[32] = "";
        if (next >= 0) snprintf(nextBuf, sizeof(nextBuf), " next change %ldm", (long)next);

        char buf[200];
        snprintf(buf, sizeof(buf),
            "%s%s | setback %s | time %s %s | rtc %s %.1fC | wifi %ddBm | up %luh%02lum | reset %s | fw %s",
            _relays.get(RELAY_PEAK) ? "PEAK" : "OFF-PEAK",
            nextBuf,
            _relays.get(RELAY_SETBACK) ? "on" : "off",
            TimeManager::sourceName(_clock.source()), _clock.tzAbbr().c_str(),
            !_clock.rtcPresent() ? "missing" : _clock.rtcLostPower() ? "lost-power" : "ok",
            isnan(_clock.rtcTempC()) ? 0.0f : _clock.rtcTempC(),
            (int)WiFi.RSSI(),
            (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60),
            resetReasonName(esp_reset_reason()), FIRMWARE_VERSION);
        return String(buf);
    }

    void schedule(JsonObject out) {
        Guard g(_lock);
        ets::scheduleToJson(_store.schedule, out);
    }

    // nullptr on success, otherwise an error message for the client.
    const char* setSchedule(JsonVariantConst json) {
        ets::WeekSchedule parsed;
        if (const char* err = ets::scheduleFromJson(json, parsed)) return err;

        Guard g(_lock);
        if (parsed != _store.schedule) {
            _store.saveSchedule(parsed);
            eventLog.add("Schedule updated");
            evaluatePeak();
        }
        return nullptr;
    }

    void setSetback(bool on) {
        Guard g(_lock);
        if (on == _store.setback) return;
        _store.saveSetback(on);
        _relays.setSetback(on);
    }

    OverrideResult startOverride(int zone) {
        if (zone < 0 || zone >= ets::OVERRIDE_ZONE_COUNT) return OverrideResult::BadZone;
        Guard g(_lock);
        return _relays.startOverride((uint8_t)zone) ? OverrideResult::Ok : OverrideResult::Busy;
    }

    static const char* resetReasonName(esp_reset_reason_t r) {
        switch (r) {
            case ESP_RST_POWERON:  return "power-on";
            case ESP_RST_EXT:      return "external";
            case ESP_RST_SW:       return "software";
            case ESP_RST_PANIC:    return "panic";
            case ESP_RST_INT_WDT:  return "interrupt-watchdog";
            case ESP_RST_TASK_WDT: return "task-watchdog";
            case ESP_RST_WDT:      return "watchdog";
            case ESP_RST_BROWNOUT: return "brownout";
            case ESP_RST_DEEPSLEEP:return "deep-sleep";
            default:               return "unknown";
        }
    }

    void requestReboot(uint32_t delayMs = 1000) {
        _rebootAt = millis() + delayMs;
        if (_rebootAt == 0) _rebootAt = 1;
    }

private:
    struct Guard {
        SemaphoreHandle_t h;
        explicit Guard(SemaphoreHandle_t lock) : h(lock) { xSemaphoreTake(h, portMAX_DELAY); }
        ~Guard() { xSemaphoreGive(h); }
    };

    SemaphoreHandle_t _lock = nullptr;
    TimeManager       _clock;
    RelayController   _relays;
    ScheduleManager   _store;
    unsigned long     _lastEval    = 0;
    bool              _timeWasValid = true;  // so a boot without time gets logged
    volatile uint32_t _rebootAt    = 0;

    static uint32_t uptimeS() { return (uint32_t)(esp_timer_get_time() / 1000000); }

    // Caller holds the lock.
    Health healthLocked() const {
        Health h;
        if (!_clock.valid())       h.add("clock not set - peak relay held in failsafe");
        if (!_clock.rtcPresent())  h.add("DS3231 RTC not found");
        else if (_clock.rtcLostPower()) h.add("RTC lost power - replace coin cell?");

        time_t last = _clock.lastNtpSync();
        if (last == 0 && uptimeS() > 3600) {
            h.add("no NTP sync since boot");
        } else if (last != 0 && ::time(nullptr) - last > 24 * 3600) {
            h.add("no NTP sync for over 24h");
        }
        return h;
    }

    // Caller holds the lock.
    void evaluatePeak() {
        _lastEval = millis();

        struct tm t;
        bool valid = _clock.localNow(t);
        if (valid != _timeWasValid) {
            _timeWasValid = valid;
            if (valid) {
                eventLog.add("Time valid - following schedule");
            } else {
                eventLog.add("Time unknown - holding failsafe (%s)", FAILSAFE_PEAK ? "peak" : "off-peak");
            }
        }

        bool peak = valid ? _store.schedule.isPeak(t.tm_wday, t.tm_hour * 60 + t.tm_min)
                          : FAILSAFE_PEAK;
        _relays.setPeak(peak);
    }
};
