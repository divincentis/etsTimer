#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>
#include <stdarg.h>

// Small in-memory ring buffer of notable events (relay changes, NTP syncs,
// boot reason...) shown on the dashboard. Also echoed to Serial.
// Safe to call from any task.
class EventLog {
public:
    static constexpr size_t CAPACITY = 40;
    static constexpr size_t TEXT_LEN = 72;

    void begin() { _lock = xSemaphoreCreateMutex(); }

    void add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char text[TEXT_LEN];
        va_list args;
        va_start(args, fmt);
        vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);

        Serial.printf("[%lu] %s\n", (unsigned long)(millis() / 1000), text);

        if (!_lock) return;
        xSemaphoreTake(_lock, portMAX_DELAY);
        Entry& e   = _entries[_next];
        e.epoch    = time(nullptr);
        e.uptimeS  = millis() / 1000;
        strlcpy(e.text, text, sizeof(e.text));
        _next      = (_next + 1) % CAPACITY;
        if (_count < CAPACITY) _count++;
        xSemaphoreGive(_lock);
    }

    // Newest first. `epoch` is 0 when the clock was not yet valid.
    void toJson(JsonArray out, time_t minValidEpoch) {
        if (!_lock) return;
        xSemaphoreTake(_lock, portMAX_DELAY);
        for (size_t i = 0; i < _count; i++) {
            const Entry& e = _entries[(_next + CAPACITY - 1 - i) % CAPACITY];
            JsonObject o = out.add<JsonObject>();
            o["epoch"]  = e.epoch >= minValidEpoch ? (uint32_t)e.epoch : 0;
            o["uptime"] = e.uptimeS;
            o["text"]   = (char*)e.text;  // non-const pointer → ArduinoJson copies it
        }
        xSemaphoreGive(_lock);
    }

private:
    struct Entry {
        time_t   epoch;
        uint32_t uptimeS;
        char     text[TEXT_LEN];
    };

    Entry             _entries[CAPACITY] = {};
    size_t            _next  = 0;
    size_t            _count = 0;
    SemaphoreHandle_t _lock  = nullptr;
};

extern EventLog eventLog;
