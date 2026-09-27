#pragma once

#include <Arduino.h>
#include "config.h"
#include "EventLog.h"
#include "OverrideSequencer.h"

enum RelayId : uint8_t {
    RELAY_PEAK = 0,
    RELAY_SETBACK,
    RELAY_OVERRIDE,
    RELAY_SEL_RL1,
    RELAY_SEL_RL23,
    RELAY_SEL_RL789,
    RELAY_WATCHDOG,
    RELAY_COUNT
};

struct RelayInfo {
    uint8_t     pin;
    const char* id;     // stable key for the API
    const char* label;  // shown in the UI
};

static const RelayInfo RELAYS[RELAY_COUNT] = {
    { PIN_RELAY_1_PEAK,      "peak",     "Peak"      },
    { PIN_RELAY_2_SETBACK,   "setback",  "Setback"   },
    { PIN_RELAY_3_OVERRIDE,  "override", "Override"  },
    { PIN_RELAY_4_SEL_RL1,   "selRl1",   "Sel RL1"   },
    { PIN_RELAY_5_SEL_RL23,  "selRl23",  "Sel RL2,3" },
    { PIN_RELAY_6_SEL_RL789, "selRl789", "Sel RL7-9" },
    { PIN_RELAY_7_WATCHDOG,  "watchdog", "Watchdog"  },
};

// Override zone → selector relay
static const RelayId ZONE_SELECTOR[ets::OVERRIDE_ZONE_COUNT] = {
    RELAY_SEL_RL1, RELAY_SEL_RL23, RELAY_SEL_RL789
};
static const char* const ZONE_LABELS[ets::OVERRIDE_ZONE_COUNT] = {
    "RL1", "RL2, RL3", "RL7, RL8, RL9"
};

// Owns the GPIOs. Only touched from the loop task.
class RelayController {
public:
    void begin() {
        // Latch the OFF level before switching each pin to output so no
        // relay glitches on at boot.
        for (const RelayInfo& r : RELAYS) {
            digitalWrite(r.pin, RELAY_OFF);
            pinMode(r.pin, OUTPUT);
        }
        // Watchdog relay: energised while healthy. Its NC contact opens and
        // the fault LED goes dark. If the ESP32 hangs or resets it drops out.
        write(RELAY_WATCHDOG, true);
    }

    bool get(RelayId id) const { return _state[id]; }

    void setPeak(bool active) {
        if (write(RELAY_PEAK, active)) {
            eventLog.add("Peak relay %s", active ? "ON (peak)" : "OFF (off-peak)");
        }
    }

    void setSetback(bool active) {
        if (write(RELAY_SETBACK, active)) {
            eventLog.add("Setback %s", active ? "ON" : "OFF");
        }
    }

    bool startOverride(uint8_t zone) {
        if (!_override.start(zone, millis())) return false;
        _lastOverrideZone = zone;
        eventLog.add("Override pulse: zone %s", ZONE_LABELS[zone]);
        applyOverride();
        return true;
    }

    bool overrideBusy()     const { return _override.busy(); }
    int  lastOverrideZone() const { return _lastOverrideZone; }

    // Call often (every loop pass) to advance the override sequence.
    void loop() {
        if (!_override.busy()) return;
        _override.tick(millis());
        applyOverride();
    }

private:
    bool _state[RELAY_COUNT] = {};
    int  _lastOverrideZone   = -1;
    ets::OverrideSequencer _override{{ OVERRIDE_SELECTOR_SETTLE_MS, OVERRIDE_PULSE_MS }};

    // Returns true if the state changed.
    bool write(RelayId id, bool on) {
        if (_state[id] == on) return false;
        _state[id] = on;
        digitalWrite(RELAYS[id].pin, on ? RELAY_ON : RELAY_OFF);
        return true;
    }

    void applyOverride() {
        int8_t zone = _override.selectorZone();
        for (uint8_t z = 0; z < ets::OVERRIDE_ZONE_COUNT; z++) {
            write(ZONE_SELECTOR[z], z == zone);
        }
        write(RELAY_OVERRIDE, _override.pulseActive());
    }
};
