#pragma once

// ─────────────────────────────────────────────
//  WiFi Credentials
// ─────────────────────────────────────────────
#define WIFI_SSID        "your_ssid"
#define WIFI_PASSWORD    "your_password"

// ─────────────────────────────────────────────
//  Admin Credentials (for /admin routes)
// ─────────────────────────────────────────────
#define ADMIN_USERNAME   "admin"
#define ADMIN_PASSWORD   "steffes"

// ─────────────────────────────────────────────
//  Device Identity
// ─────────────────────────────────────────────
#define DEVICE_NAME      "Steffes CCRP Controller"

// ─────────────────────────────────────────────
//  NTP + Timezone
//  POSIX string handles DST automatically.
//  Mountain Time: MST7MDT,M3.2.0,M11.1.0
//    MST7     = UTC-7 in winter
//    MDT      = UTC-6 in summer (implied)
//    M3.2.0   = 2nd Sunday in March  (spring forward)
//    M11.1.0  = 1st Sunday in November (fall back)
// ─────────────────────────────────────────────
#define NTP_SERVER            "pool.ntp.org"
#define NTP_SYNC_INTERVAL_MS  (60UL * 60UL * 1000UL)  // 1 hour
#define POSIX_TZ_STRING       "MST7MDT,M3.2.0,M11.1.0"

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
//  Set to LOW if your relay board is active-low
// ─────────────────────────────────────────────
#define RELAY_ON    HIGH
#define RELAY_OFF   LOW

// ─────────────────────────────────────────────
//  Override Pulse Timing (ms)
// ─────────────────────────────────────────────
#define OVERRIDE_SELECTOR_SETTLE_MS   50
#define OVERRIDE_PULSE_MS             500
