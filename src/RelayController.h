#pragma once

#include <Arduino.h>
#include "config.h"

class RelayController {
public:
    bool peakActive    = false;
    bool setbackActive = false;
    bool overrideBusy  = false;

    // Track selector state for status reporting
    int  lastOverrideZone = -1;

    void begin() {
        // Watchdog relay — energize first, before anything else
        // NC contact opens, LED goes dark = healthy
        pinMode(PIN_RELAY_7_WATCHDOG, OUTPUT);
        digitalWrite(PIN_RELAY_7_WATCHDOG, RELAY_ON);
        pinMode(PIN_RELAY_1_PEAK,      OUTPUT);
        pinMode(PIN_RELAY_2_SETBACK,   OUTPUT);
        pinMode(PIN_RELAY_3_OVERRIDE,  OUTPUT);
        pinMode(PIN_RELAY_4_SEL_RL1,   OUTPUT);
        pinMode(PIN_RELAY_5_SEL_RL23,  OUTPUT);
        pinMode(PIN_RELAY_6_SEL_RL789, OUTPUT);

        // Start all relays off
        allOff();
        Serial.println("[Relay] Initialized, all relays OFF");
    }

    void allOff() {
        digitalWrite(PIN_RELAY_1_PEAK,      RELAY_OFF);
        digitalWrite(PIN_RELAY_2_SETBACK,   RELAY_OFF);
        digitalWrite(PIN_RELAY_3_OVERRIDE,  RELAY_OFF);
        digitalWrite(PIN_RELAY_4_SEL_RL1,   RELAY_OFF);
        digitalWrite(PIN_RELAY_5_SEL_RL23,  RELAY_OFF);
        digitalWrite(PIN_RELAY_6_SEL_RL789, RELAY_OFF);
    }

    void setPeak(bool active) {
        if (peakActive == active) return;
        peakActive = active;
        digitalWrite(PIN_RELAY_1_PEAK, active ? RELAY_ON : RELAY_OFF);
        Serial.printf("[Relay] Peak: %s\n", active ? "ON" : "OFF");
    }

    void setSetback(bool active) {
        if (setbackActive == active) return;
        setbackActive = active;
        digitalWrite(PIN_RELAY_2_SETBACK, active ? RELAY_ON : RELAY_OFF);
        Serial.printf("[Relay] Setback: %s\n", active ? "ON" : "OFF");
    }

    // Fire override pulse for the given zone
    // Blocking call — runs in ~600ms total
    bool fireOverride(int zone) {
        if (overrideBusy) {
            Serial.println("[Relay] Override already in progress, ignoring");
            return false;
        }

        overrideBusy = true;
        lastOverrideZone = zone;

        uint8_t selectorPin = selectorPinForZone(zone);
        if (selectorPin == 0) {
            Serial.printf("[Relay] Unknown zone: %d\n", zone);
            overrideBusy = false;
            return false;
        }

        Serial.printf("[Relay] Override zone %d — selector closing\n", zone);

        // Step 1: Close selector
        digitalWrite(selectorPin, RELAY_ON);
        delay(OVERRIDE_SELECTOR_SETTLE_MS);

        // Step 2: Pulse override relay
        Serial.println("[Relay] Override pulse ON");
        digitalWrite(PIN_RELAY_3_OVERRIDE, RELAY_ON);
        delay(OVERRIDE_PULSE_MS);
        digitalWrite(PIN_RELAY_3_OVERRIDE, RELAY_OFF);
        Serial.println("[Relay] Override pulse OFF");

        // Step 3: Release selector
        delay(OVERRIDE_SELECTOR_SETTLE_MS);
        digitalWrite(selectorPin, RELAY_OFF);
        Serial.println("[Relay] Selector open");

        overrideBusy = false;
        return true;
    }

    // Return status as JSON string
    String statusJson() {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"peak\":%s,\"setback\":%s,\"overrideBusy\":%s,\"lastOverrideZone\":%d}",
            peakActive    ? "true" : "false",
            setbackActive ? "true" : "false",
            overrideBusy  ? "true" : "false",
            lastOverrideZone
        );
        return String(buf);
    }

private:
    uint8_t selectorPinForZone(int zone) {
        switch (zone) {
            case 0: return PIN_RELAY_4_SEL_RL1;
            case 1: return PIN_RELAY_5_SEL_RL23;
            case 2: return PIN_RELAY_6_SEL_RL789;
            default: return 0;
        }
    }
};
