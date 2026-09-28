#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "config.h"
#include "Controller.h"
#include "EventLog.h"
#include "Heartbeat.h"

// Web pages are compiled into the firmware (board_build.embed_txtfiles in
// platformio.ini), so a single .bin — flashed over USB or uploaded from the
// admin page — always carries a matching UI.
extern const char DASHBOARD_HTML[]     asm("_binary_data_www_index_html_start");
extern const char DASHBOARD_HTML_END[] asm("_binary_data_www_index_html_end");
extern const char ADMIN_HTML[]         asm("_binary_data_www_admin_schedule_html_start");
extern const char ADMIN_HTML_END[]     asm("_binary_data_www_admin_schedule_html_end");

// API
//   GET  /api/status             live state (public)
//   GET  /api/schedule           weekly schedule (public)
//   GET  /api/events             recent event log (public)
//   GET  /api/health             200 {"ok":true} or 503 with problems — for pollers
//   POST /api/setback            {"on": true|false}
//   POST /api/override           {"zone": 0|1|2}
//   POST /admin/api/schedule     schedule JSON               (admin)
//   POST /admin/api/reboot       {}                          (admin)
//   POST /admin/api/firmware     multipart .bin upload       (admin)
//
// CSRF: every POST requires either Content-Type: application/json or the
// X-ETS-Request header. Browsers can't send either cross-origin without a
// CORS preflight, which this server never approves, so a malicious web page
// can't drive the relays through a visitor's browser.
class WebUI {
public:
    WebUI() : _server(80) {}

    void begin(Controller& ctl, Heartbeat& heartbeat) {
        _auth.setUsername(ADMIN_USERNAME);
        _auth.setPassword(ADMIN_PASSWORD);
        _auth.setRealm(DEVICE_HOSTNAME);
        _auth.setAuthType(AsyncAuthType::AUTH_DIGEST);
        _auth.setAuthFailureMessage("Authentication required");
        _auth.generateHash();

        // ── Public API ──────────────────────────────────────────────────
        _server.on("/api/status", HTTP_GET, [&ctl, &heartbeat](AsyncWebServerRequest* req) {
            JsonDocument doc;
            ctl.status(doc.to<JsonObject>());
            heartbeat.toJson(doc["heartbeat"].to<JsonObject>());
            sendJson(req, 200, doc);
        });

        _server.on("/api/health", HTTP_GET, [&ctl](AsyncWebServerRequest* req) {
            Controller::Health h = ctl.health();
            JsonDocument doc;
            doc["ok"] = h.ok();
            JsonArray problems = doc["problems"].to<JsonArray>();
            for (uint8_t i = 0; i < h.count; i++) problems.add(h.problems[i]);
            sendJson(req, h.ok() ? 200 : 503, doc);
        });

        _server.on("/api/schedule", HTTP_GET, [&ctl](AsyncWebServerRequest* req) {
            JsonDocument doc;
            ctl.schedule(doc.to<JsonObject>());
            sendJson(req, 200, doc);
        });

        _server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc;
            eventLog.toJson(doc.to<JsonArray>(), MIN_VALID_EPOCH);
            sendJson(req, 200, doc);
        });

        addJsonPost("/api/setback", PROTECT_CONTROLS,
            [&ctl](AsyncWebServerRequest* req, JsonVariant& json) {
                if (!json["on"].is<bool>()) return sendError(req, 400, "expected {\"on\": true|false}");
                ctl.setSetback(json["on"].as<bool>());
                sendOk(req);
            });

        addJsonPost("/api/override", PROTECT_CONTROLS,
            [&ctl](AsyncWebServerRequest* req, JsonVariant& json) {
                if (!json["zone"].is<int>()) return sendError(req, 400, "expected {\"zone\": 0|1|2}");
                switch (ctl.startOverride(json["zone"].as<int>())) {
                    case Controller::OverrideResult::Ok:      return sendOk(req);
                    case Controller::OverrideResult::Busy:    return sendError(req, 409, "override already in progress");
                    case Controller::OverrideResult::BadZone: return sendError(req, 400, "zone must be 0, 1 or 2");
                }
            });

        // ── Admin API ───────────────────────────────────────────────────
        addJsonPost("/admin/api/schedule", true,
            [&ctl](AsyncWebServerRequest* req, JsonVariant& json) {
                if (const char* err = ctl.setSchedule(json)) return sendError(req, 400, err);
                sendOk(req);
            });

        addJsonPost("/admin/api/reboot", true,
            [&ctl](AsyncWebServerRequest* req, JsonVariant&) {
                eventLog.add("Reboot requested from web");
                ctl.requestReboot();
                sendOk(req);
            });

        _server.on("/admin/api/firmware", HTTP_POST,
            [this, &ctl](AsyncWebServerRequest* req) { finishFirmwareUpload(req, ctl); },
            [this](AsyncWebServerRequest* req, const String& filename, size_t index,
                   uint8_t* data, size_t len, bool final) {
                handleFirmwareChunk(req, filename, index, data, len, final);
            })
            .addMiddleware(&_auth);

        // ── Pages (registered last: "/admin" also matches "/admin/...") ──
        _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
            sendEmbedded(req, DASHBOARD_HTML, DASHBOARD_HTML_END);
        });

        _server.on("/admin", HTTP_GET, [](AsyncWebServerRequest* req) {
            sendEmbedded(req, ADMIN_HTML, ADMIN_HTML_END);
        }).addMiddleware(&_auth);

        _server.onNotFound([](AsyncWebServerRequest* req) {
            req->send(404, "text/plain", "Not found");
        });

        _server.begin();
        eventLog.add("Web server started on port 80");
    }

private:
    static constexpr const char* CSRF_HEADER = "X-ETS-Request";

    AsyncWebServer                _server;
    AsyncAuthenticationMiddleware _auth;

    // Firmware upload state (one upload at a time)
    AsyncWebServerRequest* _uploadReq = nullptr;
    bool                   _uploadOk  = false;
    String                 _uploadError;

    void addJsonPost(const char* uri, bool requireAdmin, ArJsonRequestHandlerFunction fn) {
        auto* h = new AsyncCallbackJsonWebHandler(uri, fn);
        h->setMethod(HTTP_POST);
        h->setMaxContentLength(4096);
        if (requireAdmin) h->addMiddleware(&_auth);
        _server.addHandler(h);
    }

    // Runs as each chunk arrives — BEFORE auth middleware, which only runs
    // once the whole body is received. So authorise here, on the first chunk,
    // or an anonymous upload could reach flash.
    void handleFirmwareChunk(AsyncWebServerRequest* req, const String& filename, size_t index,
                             uint8_t* data, size_t len, bool final) {
        if (index == 0) {
            _uploadReq = nullptr;
            _uploadOk  = false;
            _uploadError = "";
            if (!_auth.allowed(req) || !req->hasHeader(CSRF_HEADER)) return;  // middleware sends 401

            if (Update.isRunning()) Update.abort();
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                _uploadError = Update.errorString();
                return;
            }
            _uploadReq = req;
            eventLog.add("Firmware upload started: %s", filename.c_str());
        }

        if (req != _uploadReq) return;

        if (Update.write(data, len) != len) {
            _uploadError = Update.errorString();
            Update.abort();
            _uploadReq = nullptr;
            return;
        }

        if (final) {
            _uploadOk = Update.end(true);
            if (!_uploadOk) _uploadError = Update.errorString();
        }
    }

    void finishFirmwareUpload(AsyncWebServerRequest* req, Controller& ctl) {
        bool ok = (req == _uploadReq) && _uploadOk;
        _uploadReq = nullptr;

        if (!req->hasHeader(CSRF_HEADER)) return sendError(req, 403, "missing X-ETS-Request header");
        if (!ok) {
            String msg = _uploadError.length() ? _uploadError : String("upload failed");
            eventLog.add("Firmware update failed: %s", msg.c_str());
            return sendError(req, 500, msg.c_str());
        }
        eventLog.add("Firmware update OK - rebooting");
        sendOk(req);
        ctl.requestReboot(1500);
    }

    static void sendEmbedded(AsyncWebServerRequest* req, const char* start, const char* end) {
        // embed_txtfiles appends a NUL terminator; don't send it.
        req->send(200, "text/html", (const uint8_t*)start, (size_t)(end - start - 1));
    }

    static void sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc) {
        String out;
        serializeJson(doc, out);
        req->send(code, "application/json", out);
    }

    static void sendOk(AsyncWebServerRequest* req) {
        req->send(200, "application/json", "{\"ok\":true}");
    }

    static void sendError(AsyncWebServerRequest* req, int code, const char* message) {
        JsonDocument doc;
        doc["ok"]    = false;
        doc["error"] = message;
        sendJson(req, code, doc);
    }
};
