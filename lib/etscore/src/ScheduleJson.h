#pragma once

// JSON (de)serialisation for WeekSchedule.
//
// Format (days keyed by name so the file is readable by humans):
//   {
//     "days": {
//       "sun": [],
//       "mon": [ { "start": "17:00", "end": "22:00" } ],
//       ...
//     }
//   }

#include <ArduinoJson.h>
#include "WeekSchedule.h"

namespace ets {

// "HH:MM" (or "H:MM") → minutes after midnight. Strict: rejects junk.
bool parseHHMM(const char* text, uint16_t& minutes);

// minutes after midnight → "HH:MM". buf must hold at least 6 bytes.
void formatHHMM(uint16_t minutes, char* buf);

void scheduleToJson(const WeekSchedule& schedule, JsonObject out);

// Returns nullptr on success, otherwise a short error message.
// `out` is only modified on success.
const char* scheduleFromJson(JsonVariantConst in, WeekSchedule& out);

}  // namespace ets
