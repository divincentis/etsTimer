# Steffes CCRP Controller

An ESP32-based replacement for Steffes Connect that controls a CCRP (Controlled Charge Rate Program) unit via relay outputs. Designed for utilities that provide peak/off-peak time-of-use signals to Steffes ETS (Electric Thermal Storage) systems.

## Features

- Automatic peak/off-peak relay switching based on configurable schedule
- Mon–Sat / Sunday schedule (Sunday always off-peak)
- Manual setback toggle via web UI
- Override pulse firing with zone selection (RL1 / RL2,3 / RL7,8,9)
- DS3231 RTC for accurate timekeeping without WiFi
- NTP sync when WiFi is available
- Web dashboard accessible from any browser on the network
- Admin-protected schedule editing
- Tailscale-compatible for remote access

## Hardware

| Component | Notes |
|-----------|-------|
| ESP32-WROOM-32 dev board | Any standard 38-pin variant |
| DS3231 RTC module | Critical — do not rely on NTP alone |
| 8-channel 5V relay module | JD-VCC separated, optocoupler isolated |
| Project enclosure | See `hardware/` for FreeCAD files |

## GPIO Assignments

| GPIO | Function |
|------|----------|
| 26 | Relay 1 — Peak / Off-Peak |
| 27 | Relay 2 — Setback |
| 14 | Relay 3 — Override Pulse |
| 32 | Relay 4 — Selector RL1 |
| 33 | Relay 5 — Selector RL2, RL3 |
| 25 | Relay 6 — Selector RL7, RL8, RL9 |
| 21 | DS3231 SDA |
| 22 | DS3231 SCL |

> **Relay board note:** Use a JD-VCC separated board. Power relay coils from 5V, connect JD-VCC to 5V and VCC to 3.3V. This lets 3.3V ESP32 GPIOs drive optocouplers reliably.

## Setup

### 1. Configure

Edit `src/config.h`:

```cpp
#define WIFI_SSID      "your_network"
#define WIFI_PASSWORD  "your_password"
#define ADMIN_USERNAME "admin"
#define ADMIN_PASSWORD "your_admin_password"
```

### 2. Flash Firmware

```bash
# Install PlatformIO CLI or use VS Code PlatformIO extension
pio run --target upload
```

### 3. Upload Filesystem

```bash
pio run --target uploadfs
```

### 4. Access Web UI

Navigate to `http://<ESP32_IP_ADDRESS>` on your local network.

For remote access, add the ESP32 to your Tailscale network and access it by its Tailscale IP.

## Default Schedule

Based on utility rate 202.11 (General Service Whole-House Time-Of-Use):

- **Peak:** 5:00 PM – 10:00 PM, Monday through Saturday
- **Off-Peak:** All other hours, and all day Sunday

Schedule is editable at `http://<device>/admin/schedule.html` (admin credentials required).

## Override Sequencing

When an override pulse is fired:
1. Selected zone selector relay closes
2. 50ms settle delay
3. Override relay pulses for 500ms
4. Override relay opens
5. 50ms settle delay
6. Selector relay opens

## License

MIT — use freely, no warranty.
