#pragma once

// Copy this file to src/secrets.h and fill in real values.
// secrets.h is git-ignored; this example file is committed.

#define WIFI_SSID        "your_ssid"
#define WIFI_PASSWORD    "your_password"

// Protects /admin (schedule editing, firmware update, reboot).
// Uses HTTP Digest auth, so the password is never sent in the clear.
#define ADMIN_USERNAME   "admin"
#define ADMIN_PASSWORD   "change-me"

// Optional: monitoring heartbeat, e.g. a Healthchecks.io ping URL
// ("https://hc-ping.com/<uuid>"). Leave empty to disable.
#define HEARTBEAT_URL    ""
