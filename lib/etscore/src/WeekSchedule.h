#pragma once

// Weekly peak schedule — pure logic, no Arduino dependencies.
// Everything in lib/etscore is unit-tested on the host (`pio test -e native`).

#include <stdint.h>

namespace ets {

constexpr uint16_t MINUTES_PER_DAY      = 24 * 60;
constexpr uint16_t MINUTES_PER_WEEK     = 7 * MINUTES_PER_DAY;
constexpr uint8_t  MAX_WINDOWS_PER_DAY  = 4;

// A peak window in local wall-clock minutes after midnight.
// `end` is exclusive. If end < start the window crosses midnight and
// finishes on the following day (e.g. 22:00–02:00 on Monday runs until
// 02:00 Tuesday). start == end is invalid.
struct Window {
    uint16_t start;
    uint16_t end;

    bool crossesMidnight() const { return end < start; }
};

struct DaySchedule {
    uint8_t count = 0;
    Window  windows[MAX_WINDOWS_PER_DAY] = {};

    bool add(uint16_t start, uint16_t end);
};

struct WeekSchedule {
    DaySchedule days[7];  // index 0 = Sunday, matching struct tm::tm_wday

    // MPEI rate 202.11: peak 17:00–22:00 Monday through Saturday.
    static WeekSchedule defaults();

    bool isPeak(uint8_t dow, uint16_t minute) const;

    // Minutes until the peak/off-peak state next changes, or -1 if the
    // state never changes (schedule empty or peak around the clock).
    int32_t minutesUntilChange(uint8_t dow, uint16_t minute) const;

    // nullptr if valid, otherwise a short human-readable reason.
    const char* validate() const;
};

bool operator==(const WeekSchedule& a, const WeekSchedule& b);
inline bool operator!=(const WeekSchedule& a, const WeekSchedule& b) { return !(a == b); }

// Short lowercase day keys used in JSON: "sun", "mon", ...
extern const char* const DAY_KEYS[7];

}  // namespace ets
