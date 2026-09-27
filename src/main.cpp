#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <atomic>
#include <esp_task_wdt.h>

#include "config.h"
#include "Controller.h"
#include "EventLog.h"
#include "Heartbeat.h"
#include "WebUI.h"

EventLog   eventLog;
Controller controller;
WebUI      webUI;
Heartbeat  heartbeat;

// ── WiFi ─────────────────────────────────────────────────────────────────────
// Non-blocking: the schedule runs from the RTC whether or not WiFi is up.
// Events arrive on the WiFi event task; they're handled in loop().
static std::atomic<bool> s_wifiGotIp{false};
static std::atomic<bool> s_wifiLost{false};

void startWifi() {
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t) {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)       s_wifiGotIp = true;
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) s_wifiLost  = true;
    });
    WiFi.setHostname(DEVICE_HOSTNAME);  // must precede WiFi.mode() on core 2.x
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    eventLog.add("WiFi connecting to %s", WIFI_SSID);
}

void wifiLoop() {
    static bool wasConnected = false;
    static bool mdnsStarted  = false;
    static unsigned long lastReconnectAttempt = 0;

    if (s_wifiGotIp.exchange(false)) {
        wasConnected = true;
        eventLog.add("WiFi connected: %s (RSSI %d dBm)",
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
        controller.startNtp();
        if (!mdnsStarted && MDNS.begin(DEVICE_HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            mdnsStarted = true;
        }
    }

    if (s_wifiLost.exchange(false) && wasConnected) {
        wasConnected = false;
        eventLog.add("WiFi disconnected - running on RTC");
    }

    // Belt and braces: the core's auto-reconnect occasionally gives up.
    if (!WiFi.isConnected() && millis() - lastReconnectAttempt > 30000UL) {
        lastReconnectAttempt = millis();
        WiFi.reconnect();
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    eventLog.begin();
    eventLog.add("Boot: %s v%s (reset: %s)", DEVICE_NAME, FIRMWARE_VERSION,
                 Controller::resetReasonName(esp_reset_reason()));

    // Hardware watchdog — resets the ESP32 if loop() hangs. While it is
    // down, relay 7 drops out and the fault LED lights.
    esp_task_wdt_init(TASK_WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    controller.begin();  // relays (watchdog relay on), RTC, saved schedule
    startWifi();
    webUI.begin(controller, heartbeat);
    heartbeat.begin(controller);  // background task; pings once WiFi is up
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    esp_task_wdt_reset();
    controller.loop();   // clock upkeep, override sequencing, schedule → relay 1
    wifiLoop();
    delay(10);           // 10 ms tick keeps the override pulse timing tight
}
