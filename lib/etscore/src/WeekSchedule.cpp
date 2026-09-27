#include "WeekSchedule.h"

namespace ets {

const char* const DAY_KEYS[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

bool DaySchedule::add(uint16_t start, uint16_t end) {
    if (count >= MAX_WINDOWS_PER_DAY) return false;
    windows[count++] = {start, end};
    return true;
}

WeekSchedule WeekSchedule::defaults() {
    WeekSchedule s;
    for (uint8_t d = 1; d <= 6; d++) {
        s.days[d].add(17 * 60, 22 * 60);
    }
    return s;
}

bool WeekSchedule::isPeak(uint8_t dow, uint16_t minute) const {
    const DaySchedule& today = days[dow % 7];
    for (uint8_t i = 0; i < today.count; i++) {
        const Window& w = today.windows[i];
        if (w.crossesMidnight() ? minute >= w.start
                                : (minute >= w.start && minute < w.end)) {
            return true;
        }
    }

    // Tail of yesterday's windows that ran past midnight
    const DaySchedule& yesterday = days[(dow + 6) % 7];
    for (uint8_t i = 0; i < yesterday.count; i++) {
        const Window& w = yesterday.windows[i];
        if (w.crossesMidnight() && minute < w.end) {
            return true;
        }
    }
    return false;
}

int32_t WeekSchedule::minutesUntilChange(uint8_t dow, uint16_t minute) const {
    const bool current = isPeak(dow, minute);
    uint32_t t = (uint32_t)(dow % 7) * MINUTES_PER_DAY + minute;
    for (int32_t step = 1; step <= MINUTES_PER_WEEK; step++) {
        uint32_t u = (t + step) % MINUTES_PER_WEEK;
        if (isPeak(u / MINUTES_PER_DAY, u % MINUTES_PER_DAY) != current) {
            return step;
        }
    }
    return -1;
}

const char* WeekSchedule::validate() const {
    for (const DaySchedule& d : days) {
        if (d.count > MAX_WINDOWS_PER_DAY) return "too many windows in a day";
        for (uint8_t i = 0; i < d.count; i++) {
            const Window& w = d.windows[i];
            if (w.start >= MINUTES_PER_DAY || w.end >= MINUTES_PER_DAY) return "time out of range";
            if (w.start == w.end) return "window start and end are equal";
        }
    }
    return nullptr;
}

bool operator==(const WeekSchedule& a, const WeekSchedule& b) {
    for (uint8_t d = 0; d < 7; d++) {
        if (a.days[d].count != b.days[d].count) return false;
        for (uint8_t i = 0; i < a.days[d].count; i++) {
            if (a.days[d].windows[i].start != b.days[d].windows[i].start) return false;
            if (a.days[d].windows[i].end   != b.days[d].windows[i].end)   return false;
        }
    }
    return true;
}

}  // namespace ets
