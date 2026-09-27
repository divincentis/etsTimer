#pragma once

// ─────────────────────────────────────────────
//  Secrets (Wi-Fi + admin credentials)
//  Copy src/secrets.example.h → src/secrets.h and edit it.
//  secrets.h is git-ignored so real passwords never get committed.
// ─────────────────────────────────────────────
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/secrets.h not found — using placeholder credentials from secrets.example.h"
#include "secrets.example.h"
#endif

// ─────────────────────────────────────────────
//  Device Identity
// ─────────────────────────────────────────────
#define DEVICE_NAME      "Steffes CCRP Controller"
#define DEVICE_HOSTNAME  "etstimer"          // http://etstimer.local via mDNS
#define FIRMWARE_VERSION "2.0.0"

// Set to true to require the admin login for the setback toggle and
// override pulse too (the dashboard itself stays readable).
#define PROTECT_CONTROLS false

// ─────────────────────────────────────────────
//  NTP + Timezone
//  POSIX string handles DST automatically.
//  Mountain Time: MST7MDT,M3.2.0,M11.1.0
//    MST7     = UTC-7 in winter
//    MDT      = UTC-6 in summer (implied)
//    M3.2.0   = 2nd Sunday in March  (spring forward)
//    M11.1.0  = 1st Sunday in November (fall back)
// ─────────────────────────────────────────────
#define NTP_SERVER_1          "pool.ntp.org"
#define NTP_SERVER_2          "time.nist.gov"
#define NTP_SYNC_INTERVAL_MS  (60UL * 60UL * 1000UL)  // 1 hour
#define POSIX_TZ_STRING       "MST7MDT,M3.2.0,M11.1.0"

// Any clock reading before this is treated as "time not set".
#define MIN_VALID_EPOCH       1704067200UL  // 2024-01-01 00:00:00 UTC

// ─────────────────────────────────────────────
//  GPIO Pin Assignments
//  (verified safe pins for ESP32-WROOM-32)
// ─────────────────────────────────────────────
#define PIN_RELAY_1_PEAK        26   // Peak / Off-Peak
#define PIN_RELAY_2_SETBACK     27   // Setback
#define PIN_RELAY_3_OVERRIDE    14   // Override Pulse
#define PIN_RELAY_4_SEL_RL1     32   // Selector: RL1
#define PIN_RELAY_5_SEL_RL23    33   // Selector: RL2,3
#define PIN_RELAY_6_SEL_RL789   25   // Selector: RL7,8,9
#define PIN_RELAY_7_WATCHDOG    13   // NC watchdog relay — held ON when healthy

#define PIN_SDA                 21   // DS3231 SDA
#define PIN_SCL                 22   // DS3231 SCL

// ─────────────────────────────────────────────
//  Relay Logic
//  Most JD-VCC optocoupler boards are active-LOW: if relays click on when
//  they should be off, swap these two.
// ─────────────────────────────────────────────
#define RELAY_ON    HIGH
#define RELAY_OFF   LOW

// Peak relay state used when the time is unknown (RTC lost power and no NTP).
// false = off-peak, which matches what the heater sees if the ESP32 dies
// (all relays drop out), so the house keeps charging and stays warm.
#define FAILSAFE_PEAK  false

// ─────────────────────────────────────────────
//  Override Pulse Timing (ms)
// ─────────────────────────────────────────────
#define OVERRIDE_SELECTOR_SETTLE_MS   50
#define OVERRIDE_PULSE_MS             500

// ─────────────────────────────────────────────
//  Watchdog
//  If loop() stops running for this long the ESP32 resets; while it is
//  down relay 7 drops out and the fault LED lights.
// ─────────────────────────────────────────────
#define TASK_WDT_TIMEOUT_S   15
