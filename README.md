# ETS Peak Controller

An ESP32-based replacement for Steffes Connect or other ETS peak/off-peak timers. It drives an ETS control/relay unit through relay outputs: the peak signal follows a weekly time-of-use schedule, and a web dashboard gives you setback, override pulses and system health from any browser.

## Features

- **Weekly schedule**: up to 4 peak windows per day, including windows that cross midnight. The default is MPEI rate 202.11 (peak 17:00–22:00 Mon–Sat, Sunday off-peak).
- **Timekeeping that survives outages**: the DS3231 RTC holds UTC through power cuts, NTP corrects it when WiFi is up, and a POSIX TZ string handles DST.
- **Fails safe**: if the time is unknown (RTC dead and no NTP), the peak relay holds a configurable safe state. By default that's off-peak, which is also what the heater sees if the ESP32 dies.
- **Hardware watchdog**: if `loop()` hangs the ESP32 resets, and while it's down relay 7 drops out and lights a fault LED.
- **Dashboard**: live peak status with a countdown to the next change, a week-at-a-glance timeline, all 7 relay states, setback toggle, override pulse, clock/RTC/WiFi health and an activity log.
- **Admin page** (Digest auth): schedule editor with live preview, over-the-air firmware update, reboot.
- Setback state and schedule persist in NVS across reboots and firmware updates.
- Single firmware image: the web UI is compiled in, so there's no filesystem upload.
- **Offline alerts**: a heartbeat to Healthchecks.io (or similar) tells you when the device goes quiet, and flags problems like a dead RTC battery before they bite.
- `http://etstimer.local` via mDNS.

## Hardware

| Component | Notes |
|-----------|-------|
| ESP32-WROOM-32 dev board | Any standard 38-pin variant |
| DS3231 RTC module | Critical — do not rely on NTP alone. Keep a good coin cell in it. |
| 8-channel 5V relay module | JD-VCC separated, optocoupler isolated |
| Fault LED | Wired through relay 7's NC contact |
| Project enclosure | See `hardware/` for FreeCAD files (eventually) |

## GPIO Assignments

| GPIO | Function |
|------|----------|
| 26 | Relay 1 — Peak / Off-Peak (energised = peak) |
| 27 | Relay 2 — Setback |
| 14 | Relay 3 — Override Pulse |
| 32 | Relay 4 — Selector RL1 |
| 33 | Relay 5 — Selector RL2, RL3 |
| 25 | Relay 6 — Selector RL7, RL8, RL9 |
| 13 | Relay 7 — Watchdog (energised while healthy; NC contact drives the fault LED) |
| 21 | DS3231 SDA |
| 22 | DS3231 SCL |

> **Relay board note:** Use a JD-VCC separated board. Power relay coils from 5V, connect JD-VCC to 5V and VCC to 3.3V. This lets 3.3V ESP32 GPIOs drive optocouplers reliably.
>
> **Polarity:** most of these boards are *active-LOW*. If relays are on when the dashboard says off, swap `RELAY_ON` / `RELAY_OFF` in `src/config.h`.
>
> GPIO 14 can twitch briefly during boot. That's harmless here: an override pulse does nothing unless a selector relay is also closed.

## Setup

### 1. Configure

```bash
cp src/secrets.example.h src/secrets.h     # git-ignored
```

Edit `src/secrets.h`:

```cpp
#define WIFI_SSID        "your_network"
#define WIFI_PASSWORD    "your_password"
#define ADMIN_USERNAME   "admin"
#define ADMIN_PASSWORD   "your_admin_password"
```

Other settings (timezone, pins, relay polarity, failsafe state, override timing) live in `src/config.h`.

### 2. Flash Firmware

```bash
# Install PlatformIO CLI or use the VS Code PlatformIO extension
pio run --target upload
```

That's it. The web UI is embedded in the firmware, so the old `uploadfs` step is gone. The first flash must be over USB because it installs the partition table. After that you can update from the admin page.

### 3. Access Web UI

Browse to `http://etstimer.local` (or the IP shown in the serial log / your router).

For remote access, run Tailscale on another machine on the same LAN (a Raspberry Pi, NAS or router) configured as a [subnet router](https://tailscale.com/kb/1019/subnets). The ESP32 can't run the Tailscale client itself.

## Schedule

Edit it at `http://etstimer.local/admin`. Each day has up to four peak windows. A window whose end is earlier than its start runs past midnight into the next day: 22:00–02:00 on Saturday means Saturday 22:00 through Sunday 02:00. An end of 00:00 means "until midnight".

Times are local wall-clock time. The timezone and DST rules come from `POSIX_TZ_STRING` in `src/config.h` (Mountain Time by default).

## Monitoring

A device can't tell you it's offline, so something outside has to notice when it goes quiet. The controller POSTs a one-line status to a heartbeat URL every 5 minutes. A monitoring service alerts you when those pings stop, whether the ESP32 has died, the power is out or the internet is down.

### Setup with [Healthchecks.io](https://healthchecks.io) (free, or self-host it)

1. Create a check. Set **Period** to 5 minutes and **Grace** to 10 minutes.
2. Add the alert channels you want: email, SMS, Telegram, ntfy, Pushover, Slack…
3. Put the ping URL in `src/secrets.h`:
   ```cpp
   #define HEARTBEAT_URL "https://hc-ping.com/your-check-uuid"
   ```
4. Flash. The dashboard's System card shows when the last heartbeat went out.

### What triggers an alert

| Situation | What Healthchecks sees |
|-----------|------------------------|
| Device dead / hung, power cut, WiFi or internet down | Pings stop → **down** after the grace period |
| Clock not set (peak relay in failsafe), RTC missing or lost power, NTP stale >24h | `/fail` ping → **down** straight away, with the problem in the message |
| Reboot caused by watchdog, crash or brownout | One `/fail` ping saying why, then back **up** on the next ping |

Each ping body is a status line you can read in the check's log, e.g.
`OFF-PEAK next change 83m | setback off | time ntp MDT | rtc ok 23.5C | wifi -58dBm | up 3h12m | reset power-on | fw 2.1.0`.

HTTPS certificates are verified against Mozilla's root CAs embedded in the firmware (`data/cert/`, refresh with `tools/update_ca_bundle.sh`). Any service that accepts a POST to a URL works for the basic heartbeat. The `/fail` suffix is Healthchecks' convention: set `HEARTBEAT_REPORT_PROBLEMS false` in `config.h` if your service doesn't support it.

### Local monitoring

`GET /api/health` returns `200 {"ok":true}` or `503` with a list of problems. Point Home Assistant, Uptime Kuma or any HTTP monitor at it. A monitor on the same LAN can't tell you about a power cut that takes it down too, so use it alongside the heartbeat, not instead of it.

## Timekeeping

```
boot ─► DS3231 (UTC) ──► ESP32 system clock ──► localtime(TZ) ──► schedule ──► relay 1
                ▲                 ▲
                └── written back ─┴── SNTP (hourly, in the background, once WiFi is up)
```

- The RTC always stores UTC; local time and DST are computed when the time is read.
- If the RTC reports it lost power, its time is ignored until NTP corrects it.
- The dashboard shows the time source (NTP / RTC only / not set) and warns if anything is wrong.

## Override Sequencing

When an override pulse is fired:
1. Selected zone selector relay closes
2. 50ms settle delay
3. Override relay pulses for 500ms
4. Override relay opens
5. 50ms settle delay
6. Selector relay opens

The sequence runs from `loop()` without blocking, so the web server stays responsive and the dashboard shows it in progress.

## HTTP API

| Method | Path | Body | Auth |
|--------|------|------|------|
| GET  | `/api/status`   | — | — |
| GET  | `/api/schedule` | — | — |
| GET  | `/api/events`   | — | — |
| GET  | `/api/health`   | — | — |
| POST | `/api/setback`  | `{"on": true}` | optional¹ |
| POST | `/api/override` | `{"zone": 0}` (0 = RL1, 1 = RL2,3, 2 = RL7-9) | optional¹ |
| POST | `/admin/api/schedule` | `{"days": {"sun": [], "mon": [{"start": "17:00", "end": "22:00"}], …}}` | admin |
| POST | `/admin/api/reboot`   | `{}` | admin |
| POST | `/admin/api/firmware` | multipart `.bin`, header `X-ETS-Request: 1` | admin |

¹ Set `PROTECT_CONTROLS true` in `config.h` to require the admin login for these.

POST bodies must be `Content-Type: application/json`. Browsers can't send that cross-origin without a CORS preflight, which the device never approves, so other web pages can't drive your relays through your browser.

Example:

```bash
curl -X POST -H 'Content-Type: application/json' -d '{"on":true}' http://etstimer.local/api/setback
curl --digest -u admin:PASSWORD -X POST -H 'Content-Type: application/json' \
     -d @schedule.json http://etstimer.local/admin/api/schedule
```

## Development

```
src/            firmware (Arduino, header-only classes)
lib/etscore/    schedule + override logic — plain C++, unit-tested on the host
test/           doctest unit tests
data/www/       dashboard + admin pages (embedded into the firmware)
tools/          mock_server.py — fake device for UI work without hardware
                update_ca_bundle.sh — refresh the embedded root CAs
```

```bash
pio test -e native              # unit tests on your computer
python3 tools/mock_server.py    # UI at http://localhost:8080 against a fake device
```

GitHub Actions runs the tests and builds the firmware on every push. The built `firmware.bin` is attached to each run as an artifact, ready for the admin page's firmware update.

## License

MIT — use freely, no warranty.
