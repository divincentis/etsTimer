#pragma once

// Non-blocking override pulse sequence — pure logic, no Arduino dependencies.
//
//   start(zone) ─► selector ON ─(settle)─► pulse ON ─(pulse)─► pulse OFF
//               ─(settle)─► selector OFF ─► idle
//
// The caller ticks it with the current millisecond clock and drives the
// relays from selectorZone() / pulseActive(). Each phase lasts at least its
// configured duration, even if tick() is called late.

#include <stdint.h>

namespace ets {

constexpr uint8_t OVERRIDE_ZONE_COUNT = 3;

struct OverrideTiming {
    uint32_t settleMs;
    uint32_t pulseMs;
};

class OverrideSequencer {
public:
    explicit OverrideSequencer(OverrideTiming timing) : _timing(timing) {}

    // Returns false if a sequence is already running or the zone is invalid.
    bool start(uint8_t zone, uint32_t nowMs);
    void tick(uint32_t nowMs);

    bool busy() const { return _phase != Phase::Idle; }

    // Zone whose selector relay should be energised, or -1 for none.
    int8_t selectorZone() const { return busy() ? (int8_t)_zone : -1; }
    bool   pulseActive()  const { return _phase == Phase::Pulse; }

private:
    enum class Phase : uint8_t { Idle, SelectorSettle, Pulse, ReleaseSettle };

    OverrideTiming _timing;
    Phase    _phase      = Phase::Idle;
    uint8_t  _zone       = 0;
    uint32_t _phaseStart = 0;

    void enter(Phase p, uint32_t nowMs) { _phase = p; _phaseStart = nowMs; }
};

}  // namespace ets
