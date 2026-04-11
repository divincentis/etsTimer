#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ScheduleManager.h"
#include "RelayController.h"
#include "TimeManager.h"

class WebUI {
public:
    AsyncWebServer server;

    WebUI() : server(80) {}

    void begin(ScheduleManager& sched, RelayController& relay, TimeManager& timeMgr) {

        // ── Static files from LittleFS ──────────────────────────────────
        server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

        // ── API: Status (open) ──────────────────────────────────────────
        server.on("/api/status", HTTP_GET, [&](AsyncWebServerRequest* req) {
            DateTime now = timeMgr.nowLocal();
            bool peak    = sched.isPeak(now);

            JsonDocument doc;
            doc["time"]        = timeMgr.timeString();
            doc["dow"]         = timeMgr.dowString();
            doc["peak"]        = relay.peakActive;
            doc["setback"]     = relay.setbackActive;
            doc["ntpSynced"]   = timeMgr.ntpSynced;
            doc["rtcTemp"]     = timeMgr.rtcTemperature();
            doc["overrideBusy"]= relay.overrideBusy;
            doc["deviceName"]  = DEVICE_NAME;
            doc["tz"]          = timeMgr.tzAbbr();

            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── API: Toggle Setback (open) ──────────────────────────────────
        server.on("/api/setback", HTTP_POST, [&](AsyncWebServerRequest* req) {
            relay.setSetback(!relay.setbackActive);
            req->send(200, "application/json",
                relay.setbackActive ? "{\"setback\":true}" : "{\"setback\":false}");
        });

        // ── API: Fire Override (open) ───────────────────────────────────
        server.on("/api/override", HTTP_POST, [&](AsyncWebServerRequest* req) {
            if (!req->hasParam("zone", true)) {
                req->send(400, "application/json", "{\"error\":\"zone required\"}");
                return;
            }

            int zone = req->getParam("zone", true)->value().toInt();
            if (zone < 0 || zone > 2) {
                req->send(400, "application/json", "{\"error\":\"zone must be 0, 1, or 2\"}");
                return;
            }

            if (relay.overrideBusy) {
                req->send(409, "application/json", "{\"error\":\"override in progress\"}");
                return;
            }

            // Fire async — don't block the web server
            // Note: fireOverride is blocking ~600ms; for production consider a task queue
            relay.fireOverride(zone);
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ── ADMIN: Get Schedule ─────────────────────────────────────────
        server.on("/admin/api/schedule", HTTP_GET, [&](AsyncWebServerRequest* req) {
            if (!authenticate(req)) return;
            req->send(200, "application/json", sched.toJson());
        });

        // ── ADMIN: Save Schedule ────────────────────────────────────────
        AsyncCallbackJsonWebHandler* schedHandler =
            new AsyncCallbackJsonWebHandler("/admin/api/schedule",
                [&](AsyncWebServerRequest* req, JsonVariant& json) {
                    if (!authenticate(req)) return;

                    JsonObject obj = json.as<JsonObject>();

                    if (obj["monSat"].is<JsonObject>()) {
                        sched.current.monSat.startHour   = obj["monSat"]["startHour"]   | 17;
                        sched.current.monSat.startMinute = obj["monSat"]["startMinute"] | 0;
                        sched.current.monSat.endHour     = obj["monSat"]["endHour"]     | 22;
                        sched.current.monSat.endMinute   = obj["monSat"]["endMinute"]   | 0;
                    }

                    if (obj["sundayAlwaysOffPeak"].is<bool>()) {
                        sched.current.sundayAlwaysOffPeak = obj["sundayAlwaysOffPeak"];
                    }

                    if (obj["timezone"].is<const char*>()) {
                        sched.current.timezone = obj["timezone"].as<String>();
                    }

                    if (obj["utcOffset"].is<int>()) {
                        sched.current.utcOffsetHours = obj["utcOffset"];
                    }

                    sched.save();
                    req->send(200, "application/json", "{\"ok\":true}");
                });
        server.addHandler(schedHandler);

        // ── 404 ─────────────────────────────────────────────────────────
        server.onNotFound([](AsyncWebServerRequest* req) {
            req->send(404, "text/plain", "Not found");
        });

        server.begin();
        Serial.println("[Web] Server started on port 80");
    }

private:
    bool authenticate(AsyncWebServerRequest* req) {
        if (!req->authenticate(ADMIN_USERNAME, ADMIN_PASSWORD)) {
            req->requestAuthentication();
            return false;
        }
        return true;
    }
};
