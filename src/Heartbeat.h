#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "Controller.h"
#include "EventLog.h"

// Root CAs for HTTPS certificate checks (Mozilla list via certifi).
// Regenerate with tools/update_ca_bundle.sh.
extern const uint8_t CA_BUNDLE[] asm("_binary_data_cert_x509_crt_bundle_start");

// Dead-man's switch: POSTs a one-line status to HEARTBEAT_URL every
// HEARTBEAT_INTERVAL_S. The monitoring service alerts when pings stop
// arriving, which is the only way to learn the device is offline.
//
// Runs in its own task so a slow or dead network can never stall loop()
// (relay timing, watchdog).
class Heartbeat {
public:
    bool enabled() const { return HEARTBEAT_URL[0] != '\0'; }

    void begin(Controller& ctl) {
        if (!enabled()) {
            eventLog.add("Heartbeat disabled (no HEARTBEAT_URL)");
            return;
        }
        _ctl = &ctl;
        _pendingCrashReport = isCrashReset(esp_reset_reason());
        xTaskCreate(taskEntry, "heartbeat", 16384, this, 1, nullptr);
    }

    // For /api/status. Safe from any task.
    void toJson(JsonObject out) {
        Result r;
        portENTER_CRITICAL(&_mux);
        r = _result;
        portEXIT_CRITICAL(&_mux);

        uint32_t now = uptimeS();
        out["enabled"]  = enabled();
        out["interval"] = HEARTBEAT_INTERVAL_S;
        out["lastOkAge"]   = r.lastOkS      ? (int32_t)(now - r.lastOkS)      : -1;
        out["lastTryAge"]  = r.lastAttemptS ? (int32_t)(now - r.lastAttemptS) : -1;
        out["lastCode"]    = r.lastCode;
        out["reportedFail"] = r.reportedFail;
    }

private:
    struct Result {
        uint32_t lastAttemptS = 0;  // uptime seconds; 0 = never
        uint32_t lastOkS      = 0;
        int      lastCode     = 0;  // HTTP status, or negative HTTPClient error
        bool     reportedFail = false;
    };

    Controller*  _ctl = nullptr;
    bool         _pendingCrashReport = false;
    Result       _result;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

    static void taskEntry(void* self) { static_cast<Heartbeat*>(self)->run(); }

    void run() {
        bool lastOk = true;  // only log transitions
        for (;;) {
            if (!WiFi.isConnected()) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            Controller::Health health = _ctl->health();
            String body = _ctl->summary();

            if (_pendingCrashReport) {
                body = String("REBOOTED after ") + resetReasonText() + " | " + body;
            }
            if (!health.ok()) {
                String problems;
                for (uint8_t i = 0; i < health.count; i++) {
                    if (i) problems += "; ";
                    problems += health.problems[i];
                }
                body = "PROBLEM: " + problems + " | " + body;
            }
            bool fail = HEARTBEAT_REPORT_PROBLEMS && (_pendingCrashReport || !health.ok());

            String url = String(HEARTBEAT_URL) + (fail ? "/fail" : "");
            int code = post(url, body);
            bool ok = code >= 200 && code < 300;

            portENTER_CRITICAL(&_mux);
            _result.lastAttemptS = uptimeS();
            _result.lastCode     = code;
            if (ok) {
                _result.lastOkS      = _result.lastAttemptS;
                _result.reportedFail = fail;
            }
            portEXIT_CRITICAL(&_mux);

            if (ok) _pendingCrashReport = false;  // delivered
            if (ok != lastOk) {
                lastOk = ok;
                if (ok) eventLog.add("Heartbeat OK again");
                else    eventLog.add("Heartbeat failed (%s)", describe(code).c_str());
            }

            vTaskDelay(pdMS_TO_TICKS(ok ? HEARTBEAT_INTERVAL_S * 1000UL : 60000UL));
        }
    }

    // Returns the HTTP status code, or a negative HTTPClient error.
    static int post(const String& url, const String& body) {
        WiFiClientSecure tls;
        WiFiClient       plain;
        bool https = url.startsWith("https://");
        if (https) {
            tls.setCACertBundle(CA_BUNDLE);
            tls.setHandshakeTimeout(15);
        }

        HTTPClient http;
        http.setConnectTimeout(10000);
        http.setTimeout(10000);
        http.setUserAgent(String(DEVICE_HOSTNAME) + "/" FIRMWARE_VERSION);
        if (!http.begin(https ? static_cast<WiFiClient&>(tls) : plain, url)) {
            return HTTPC_ERROR_CONNECTION_REFUSED;
        }
        http.addHeader("Content-Type", "text/plain");
        int code = http.POST(body);
        http.end();
        return code;
    }

    static uint32_t uptimeS() { return (uint32_t)(esp_timer_get_time() / 1000000); }

    static String describe(int code) {
        return code > 0 ? "HTTP " + String(code) : HTTPClient::errorToString(code);
    }

    static bool isCrashReset(esp_reset_reason_t r) {
        return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
               r == ESP_RST_WDT   || r == ESP_RST_BROWNOUT;
    }

    static const char* resetReasonText() {
        return Controller::resetReasonName(esp_reset_reason());
    }
};
