#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>
#include <atomic>
#include "config.h"
#include "EventLog.h"

// One clock for the whole system: the ESP32 system clock (time()).
//
//   boot:  DS3231 (stores UTC) ──settimeofday──► system clock
//   NTP:   SNTP runs in the background, updates the system clock, and on
//          every sync we write UTC back to the DS3231.
//
// Local time + DST come from POSIX_TZ_STRING via localtime_r(). We use
// configTzTime() rather than configTime(): configTime() overwrites the TZ
// environment variable with a fixed UTC offset, which would silently turn
// the schedule into UTC.
class TimeManager {
public:
    enum class Source : uint8_t { None, Rtc, Ntp };

    void begin() {
        setenv("TZ", POSIX_TZ_STRING, 1);
        tzset();

        Wire.begin(PIN_SDA, PIN_SCL);
        _rtcPresent = _rtc.begin(&Wire);

        if (!_rtcPresent) {
            eventLog.add("DS3231 RTC not found - waiting for NTP");
        } else {
            _rtcLostPower = _rtc.lostPower();
            uint32_t rtcEpoch = _rtc.now().unixtime();
            if (_rtcLostPower) {
                eventLog.add("RTC lost power - time unknown until NTP sync");
            } else if (rtcEpoch < MIN_VALID_EPOCH) {
                eventLog.add("RTC time implausible - waiting for NTP");
            } else {
                struct timeval tv = { (time_t)rtcEpoch, 0 };
                settimeofday(&tv, nullptr);
                _source = Source::Rtc;
                eventLog.add("Clock set from RTC: %s", localString().c_str());
            }
            _rtcTempC = _rtc.getTemperature();
        }

        // Must be configured before SNTP starts.
        sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
        sntp_set_time_sync_notification_cb(onNtpSync);
    }

    // Start background NTP. Call once the network is up; later calls are no-ops.
    // SNTP keeps retrying and re-syncing on its own after this.
    void startNtp() {
        if (_ntpStarted) return;
        _ntpStarted = true;
        configTzTime(POSIX_TZ_STRING, NTP_SERVER_1, NTP_SERVER_2);
        eventLog.add("NTP started (%s)", NTP_SERVER_1);
    }

    // Call from loop(). All I2C traffic happens here, on the loop task.
    void loop() {
        if (s_ntpSyncPending.exchange(false)) {
            time_t now = time(nullptr);
            _source      = Source::Ntp;
            _lastNtpSync = now;
            if (_rtcPresent) {
                _rtc.adjust(DateTime((uint32_t)now));  // UTC; also clears the lost-power flag
                _rtcLostPower = false;
            }
            if (!_loggedFirstSync) {
                _loggedFirstSync = true;
                eventLog.add("NTP sync OK: %s", localString().c_str());
            }
        }

        if (_rtcPresent && millis() - _lastTempRead > 60000UL) {
            _lastTempRead = millis();
            _rtcTempC = _rtc.getTemperature();
        }
    }

    bool valid() const { return time(nullptr) >= (time_t)MIN_VALID_EPOCH; }

    // Fills `out` with local time; returns false if the clock is not set.
    bool localNow(struct tm& out) const {
        time_t now = time(nullptr);
        localtime_r(&now, &out);
        return now >= (time_t)MIN_VALID_EPOCH;
    }

    // "2026-09-27T14:03:11"
    String localString() const {
        struct tm t;
        localNow(t);
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &t);
        return String(buf);
    }

    // "MST" / "MDT"
    String tzAbbr() const {
        struct tm t;
        localNow(t);
        char buf[8];
        strftime(buf, sizeof(buf), "%Z", &t);
        return String(buf);
    }

    Source source()       const { return _source; }
    time_t lastNtpSync()  const { return _lastNtpSync; }
    bool   rtcPresent()   const { return _rtcPresent; }
    bool   rtcLostPower() const { return _rtcLostPower; }
    float  rtcTempC()     const { return _rtcTempC; }

    static const char* sourceName(Source s) {
        switch (s) {
            case Source::Rtc: return "rtc";
            case Source::Ntp: return "ntp";
            default:          return "none";
        }
    }

private:
    RTC_DS3231 _rtc;
    bool   _rtcPresent      = false;
    bool   _rtcLostPower    = false;
    bool   _ntpStarted      = false;
    bool   _loggedFirstSync = false;
    Source _source          = Source::None;
    time_t _lastNtpSync     = 0;
    float  _rtcTempC        = NAN;
    unsigned long _lastTempRead = 0;

    // Set from the lwIP task; consumed on the loop task.
    static inline std::atomic<bool> s_ntpSyncPending{false};
    static void onNtpSync(struct timeval*) { s_ntpSyncPending = true; }
};
