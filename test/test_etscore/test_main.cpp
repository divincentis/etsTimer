// Host-side unit tests for lib/etscore.  Run with:  pio test -e native

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <ArduinoJson.h>
#include <string>

#include "OverrideSequencer.h"
#include "ScheduleJson.h"
#include "WeekSchedule.h"

using namespace ets;

static constexpr uint8_t SUN = 0, MON = 1, TUE = 2, SAT = 6;
static uint16_t hm(int h, int m) { return h * 60 + m; }

// ── WeekSchedule ────────────────────────────────────────────────────────────

TEST_CASE("default schedule matches MPEI 202.11") {
    WeekSchedule s = WeekSchedule::defaults();
    CHECK(s.validate() == nullptr);

    for (uint8_t d = MON; d <= SAT; d++) {
        CHECK_FALSE(s.isPeak(d, hm(16, 59)));
        CHECK(s.isPeak(d, hm(17, 0)));
        CHECK(s.isPeak(d, hm(21, 59)));
        CHECK_FALSE(s.isPeak(d, hm(22, 0)));
        CHECK_FALSE(s.isPeak(d, hm(0, 0)));
    }
    for (uint16_t m = 0; m < MINUTES_PER_DAY; m++) {
        CHECK_FALSE(s.isPeak(SUN, m));
    }
}

TEST_CASE("window crossing midnight belongs to the day it starts") {
    WeekSchedule s;
    s.days[MON].add(hm(22, 0), hm(2, 0));

    CHECK_FALSE(s.isPeak(MON, hm(1, 0)));  // Monday early morning: not Sunday's window
    CHECK(s.isPeak(MON, hm(22, 0)));
    CHECK(s.isPeak(MON, hm(23, 59)));
    CHECK(s.isPeak(TUE, hm(0, 0)));
    CHECK(s.isPeak(TUE, hm(1, 59)));
    CHECK_FALSE(s.isPeak(TUE, hm(2, 0)));
    CHECK_FALSE(s.isPeak(TUE, hm(22, 0)));
}

TEST_CASE("Saturday overnight window spills into Sunday") {
    WeekSchedule s;
    s.days[SAT].add(hm(23, 0), hm(1, 0));
    CHECK(s.isPeak(SUN, hm(0, 30)));
    CHECK_FALSE(s.isPeak(SUN, hm(1, 0)));
}

TEST_CASE("window ending at 00:00 runs to midnight") {
    WeekSchedule s;
    s.days[MON].add(hm(20, 0), 0);
    CHECK(s.isPeak(MON, hm(23, 59)));
    CHECK_FALSE(s.isPeak(TUE, 0));
}

TEST_CASE("multiple windows per day") {
    WeekSchedule s;
    s.days[MON].add(hm(7, 0), hm(9, 0));
    s.days[MON].add(hm(17, 0), hm(21, 0));
    CHECK(s.isPeak(MON, hm(8, 0)));
    CHECK_FALSE(s.isPeak(MON, hm(12, 0)));
    CHECK(s.isPeak(MON, hm(18, 0)));
}

TEST_CASE("DaySchedule rejects more than MAX_WINDOWS_PER_DAY") {
    DaySchedule d;
    for (int i = 0; i < MAX_WINDOWS_PER_DAY; i++) CHECK(d.add(i * 60, i * 60 + 30));
    CHECK_FALSE(d.add(600, 660));
    CHECK(d.count == MAX_WINDOWS_PER_DAY);
}

TEST_CASE("validate rejects bad windows") {
    WeekSchedule s;
    s.days[MON].add(hm(10, 0), hm(10, 0));
    CHECK(s.validate() != nullptr);

    WeekSchedule t;
    t.days[MON].add(MINUTES_PER_DAY, 5);
    CHECK(t.validate() != nullptr);
}

TEST_CASE("minutesUntilChange") {
    WeekSchedule s = WeekSchedule::defaults();

    CHECK(s.minutesUntilChange(MON, hm(16, 0)) == 60);   // peak starts at 17:00
    CHECK(s.minutesUntilChange(MON, hm(17, 0)) == 300);  // peak ends at 22:00
    CHECK(s.minutesUntilChange(MON, hm(21, 59)) == 1);
    // Saturday 22:00 → Monday 17:00 (Sunday has no peak)
    //   2h to Saturday midnight + 24h Sunday + 17h Monday = 43h
    CHECK(s.minutesUntilChange(SAT, hm(22, 0)) == 43 * 60);

    WeekSchedule empty;
    CHECK(empty.minutesUntilChange(MON, 0) == -1);

    WeekSchedule always;
    for (uint8_t d = 0; d < 7; d++) {
        always.days[d].add(0, hm(12, 0));
        always.days[d].add(hm(12, 0), 0);  // 12:00 → midnight
    }
    CHECK(always.minutesUntilChange(TUE, hm(12, 0)) == -1);
}

// ── JSON ────────────────────────────────────────────────────────────────────

TEST_CASE("parseHHMM") {
    uint16_t m = 0;
    CHECK(parseHHMM("00:00", m)); CHECK(m == 0);
    CHECK(parseHHMM("17:30", m)); CHECK(m == hm(17, 30));
    CHECK(parseHHMM("7:05", m));  CHECK(m == hm(7, 5));
    CHECK(parseHHMM("23:59", m)); CHECK(m == hm(23, 59));

    CHECK_FALSE(parseHHMM(nullptr, m));
    CHECK_FALSE(parseHHMM("", m));
    CHECK_FALSE(parseHHMM("24:00", m));
    CHECK_FALSE(parseHHMM("12:60", m));
    CHECK_FALSE(parseHHMM("12:5", m));
    CHECK_FALSE(parseHHMM("12:055", m));
    CHECK_FALSE(parseHHMM("123:00", m));
    CHECK_FALSE(parseHHMM("12-00", m));
    CHECK_FALSE(parseHHMM(" 12:00", m));
}

TEST_CASE("formatHHMM") {
    char buf[6];
    formatHHMM(0, buf);          CHECK(std::string(buf) == "00:00");
    formatHHMM(hm(17, 5), buf);  CHECK(std::string(buf) == "17:05");
    formatHHMM(hm(23, 59), buf); CHECK(std::string(buf) == "23:59");
}

TEST_CASE("schedule JSON round trip") {
    WeekSchedule s = WeekSchedule::defaults();
    s.days[SUN].add(hm(22, 0), hm(2, 0));

    JsonDocument doc;
    scheduleToJson(s, doc.to<JsonObject>());

    std::string text;
    serializeJson(doc, text);
    CHECK(text.find("\"mon\":[{\"start\":\"17:00\",\"end\":\"22:00\"}]") != std::string::npos);

    JsonDocument parsedDoc;
    REQUIRE(deserializeJson(parsedDoc, text) == DeserializationError::Ok);
    WeekSchedule back;
    CHECK(scheduleFromJson(parsedDoc.as<JsonVariantConst>(), back) == nullptr);
    CHECK(back == s);
}

TEST_CASE("scheduleFromJson rejects bad input and leaves output untouched") {
    const char* bad[] = {
        "{}",
        "{\"days\":{}}",
        "{\"days\":{\"sun\":[],\"mon\":[],\"tue\":[],\"wed\":[],\"thu\":[],\"fri\":[]}}",
        "{\"days\":{\"sun\":[],\"mon\":[{\"start\":\"25:00\",\"end\":\"22:00\"}],\"tue\":[],\"wed\":[],\"thu\":[],\"fri\":[],\"sat\":[]}}",
        "{\"days\":{\"sun\":[],\"mon\":[{\"start\":\"10:00\",\"end\":\"10:00\"}],\"tue\":[],\"wed\":[],\"thu\":[],\"fri\":[],\"sat\":[]}}",
        "{\"days\":{\"sun\":[],\"mon\":[{\"start\":1020,\"end\":1320}],\"tue\":[],\"wed\":[],\"thu\":[],\"fri\":[],\"sat\":[]}}",
        "{\"days\":{\"sun\":[],\"mon\":[1,2,3,4,5],\"tue\":[],\"wed\":[],\"thu\":[],\"fri\":[],\"sat\":[]}}",
        "{\"days\":{\"sun\":[],\"mon\":[\"17:00\"],\"tue\":[],\"wed\":[],\"thu\":[],\"fri\":[],\"sat\":[]}}",
    };
    for (const char* text : bad) {
        CAPTURE(text);
        JsonDocument doc;
        REQUIRE(deserializeJson(doc, text) == DeserializationError::Ok);
        WeekSchedule out = WeekSchedule::defaults();
        CHECK(scheduleFromJson(doc.as<JsonVariantConst>(), out) != nullptr);
        CHECK(out == WeekSchedule::defaults());
    }
}

// ── OverrideSequencer ───────────────────────────────────────────────────────

TEST_CASE("override sequence timing") {
    OverrideSequencer seq({50, 500});
    CHECK_FALSE(seq.busy());
    CHECK(seq.selectorZone() == -1);

    REQUIRE(seq.start(1, 1000));
    CHECK(seq.busy());
    CHECK(seq.selectorZone() == 1);
    CHECK_FALSE(seq.pulseActive());

    CHECK_FALSE(seq.start(0, 1001));  // busy — rejected

    seq.tick(1049); CHECK_FALSE(seq.pulseActive());
    seq.tick(1050); CHECK(seq.pulseActive());      CHECK(seq.selectorZone() == 1);
    seq.tick(1549); CHECK(seq.pulseActive());
    seq.tick(1550); CHECK_FALSE(seq.pulseActive()); CHECK(seq.selectorZone() == 1);
    seq.tick(1599); CHECK(seq.busy());
    seq.tick(1600); CHECK_FALSE(seq.busy());        CHECK(seq.selectorZone() == -1);

    CHECK(seq.start(2, 2000));
}

TEST_CASE("override rejects invalid zone") {
    OverrideSequencer seq({50, 500});
    CHECK_FALSE(seq.start(3, 0));
    CHECK_FALSE(seq.busy());
}

TEST_CASE("late ticks never skip a phase or shorten the pulse") {
    OverrideSequencer seq({50, 500});
    seq.start(0, 0);
    seq.tick(10000);  // very late: only advances to Pulse
    CHECK(seq.pulseActive());
    seq.tick(10499); CHECK(seq.pulseActive());
    seq.tick(10500); CHECK_FALSE(seq.pulseActive());
}

TEST_CASE("override survives millis() rollover") {
    OverrideSequencer seq({50, 500});
    uint32_t t0 = 0xFFFFFFFFu - 20;
    seq.start(0, t0);
    seq.tick(t0 + 49); CHECK_FALSE(seq.pulseActive());
    seq.tick(t0 + 50); CHECK(seq.pulseActive());  // wrapped past zero
}

int main(int argc, char** argv) {
    doctest::Context context;
    // Required by PlatformIO's doctest runner, which parses the output
    context.setOption("success", true);
    context.setOption("no-exitcode", true);
    context.applyCommandLine(argc, argv);
    return context.run();
}
