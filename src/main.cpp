#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "TimeManager.h"
#include "ScheduleManager.h"
#include "RelayController.h"
#include "WebUI.h"

TimeManager     timeMgr;
ScheduleManager sched;
RelayController relay;
WebUI           webUI;

// ── WiFi ─────────────────────────────────────────────────────────────────────
void connectWifi() {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WiFi] Connection failed — running offline with RTC");
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] Steffes CCRP Controller starting...");

    // Hardware watchdog — 30s timeout, resets ESP32 if loop() hangs
    // This also releases the NC watchdog relay, lighting the fault LED
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    // Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("[Boot] LittleFS mount failed!");
    } else {
        Serial.println("[Boot] LittleFS mounted");
    }

    // Hardware init
    relay.begin();       // Energizes watchdog relay (relay 7) — LED goes dark
    timeMgr.begin();     // RTC + sets POSIX TZ for DST
    sched.begin();       // Load schedule from LittleFS

    // Network
    connectWifi();

    // NTP sync if WiFi available — no offset arg needed, TZ handles it
    if (WiFi.status() == WL_CONNECTED) {
        timeMgr.syncNTP();
    }

    // Web server
    webUI.begin(sched, relay, timeMgr);

    Serial.println("[Boot] Ready.");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    // Kick hardware watchdog — if this stops, ESP32 resets, relay 7 drops, LED lights
    esp_task_wdt_reset();

    // Periodic NTP re-sync
    timeMgr.loop();

    // Evaluate schedule against DST-correct local time, drive relay 1
    DateTime localNow = timeMgr.nowLocal();
    relay.setPeak(sched.isPeak(localNow));

    // WiFi watchdog — reconnect if dropped
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 30000) {
            Serial.println("[WiFi] Reconnecting...");
            WiFi.reconnect();
            lastReconnectAttempt = millis();
        }
    }

    delay(1000); // 1s tick — plenty for schedule granularity
}
