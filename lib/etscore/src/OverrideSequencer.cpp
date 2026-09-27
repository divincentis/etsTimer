#include "OverrideSequencer.h"

namespace ets {

bool OverrideSequencer::start(uint8_t zone, uint32_t nowMs) {
    if (busy() || zone >= OVERRIDE_ZONE_COUNT) return false;
    _zone = zone;
    enter(Phase::SelectorSettle, nowMs);
    return true;
}

void OverrideSequencer::tick(uint32_t nowMs) {
    // Unsigned subtraction handles millis() rollover.
    uint32_t elapsed = nowMs - _phaseStart;

    // Advance at most one phase per tick so every phase is observed by the
    // caller and gets its full duration on the relays.
    switch (_phase) {
        case Phase::Idle:
            break;
        case Phase::SelectorSettle:
            if (elapsed >= _timing.settleMs) enter(Phase::Pulse, nowMs);
            break;
        case Phase::Pulse:
            if (elapsed >= _timing.pulseMs) enter(Phase::ReleaseSettle, nowMs);
            break;
        case Phase::ReleaseSettle:
            if (elapsed >= _timing.settleMs) enter(Phase::Idle, nowMs);
            break;
    }
}

}  // namespace ets
