#include "ScheduleJson.h"

namespace ets {

bool parseHHMM(const char* text, uint16_t& minutes) {
    if (!text) return false;

    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    const char* p = text;

    uint16_t hours = 0;
    int hourDigits = 0;
    while (isDigit(*p) && hourDigits < 2) {
        hours = hours * 10 + (*p++ - '0');
        hourDigits++;
    }
    if (hourDigits == 0 || *p++ != ':') return false;
    if (!isDigit(p[0]) || !isDigit(p[1]) || p[2] != '\0') return false;

    uint16_t mins = (p[0] - '0') * 10 + (p[1] - '0');
    if (hours > 23 || mins > 59) return false;

    minutes = hours * 60 + mins;
    return true;
}

void formatHHMM(uint16_t minutes, char* buf) {
    uint16_t h = (minutes / 60) % 24;
    uint16_t m = minutes % 60;
    buf[0] = '0' + h / 10;
    buf[1] = '0' + h % 10;
    buf[2] = ':';
    buf[3] = '0' + m / 10;
    buf[4] = '0' + m % 10;
    buf[5] = '\0';
}

void scheduleToJson(const WeekSchedule& schedule, JsonObject out) {
    JsonObject days = out["days"].to<JsonObject>();
    for (uint8_t d = 0; d < 7; d++) {
        JsonArray arr = days[DAY_KEYS[d]].to<JsonArray>();
        const DaySchedule& day = schedule.days[d];
        for (uint8_t i = 0; i < day.count; i++) {
            char start[6], end[6];
            formatHHMM(day.windows[i].start, start);
            formatHHMM(day.windows[i].end, end);
            JsonObject w = arr.add<JsonObject>();
            w["start"] = start;
            w["end"]   = end;
        }
    }
}

const char* scheduleFromJson(JsonVariantConst in, WeekSchedule& out) {
    JsonObjectConst days = in["days"];
    if (days.isNull()) return "missing \"days\" object";

    WeekSchedule parsed;
    for (uint8_t d = 0; d < 7; d++) {
        JsonArrayConst arr = days[DAY_KEYS[d]];
        if (arr.isNull()) return "every day (sun..sat) must be present as an array";
        if (arr.size() > MAX_WINDOWS_PER_DAY) return "too many windows in a day (max 4)";

        for (JsonObjectConst w : arr) {
            uint16_t start, end;
            if (!parseHHMM(w["start"], start)) return "invalid start time (expected HH:MM)";
            if (!parseHHMM(w["end"], end))     return "invalid end time (expected HH:MM)";
            if (!parsed.days[d].add(start, end)) return "too many windows in a day (max 4)";
        }
    }

    if (const char* err = parsed.validate()) return err;
    out = parsed;
    return nullptr;
}

}  // namespace ets
