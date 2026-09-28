# Deployment Guide

How to take the ETS Peak Controller from a fresh checkout to a verified installation on your heater, and how to update it afterwards.

**Plan on about two hours:** 30 min to build and configure, 60 min of bench testing, 30 min to install. Do the bench tests before you touch the heater wiring. Every check in section 3 is much easier on your desk than in a utility room.

> **Safety.** This device switches the control inputs of a heating appliance. Turn the heater off at its breaker before working on its wiring. If anything you connect to carries mains voltage, or you're unsure, have a licensed electrician do the installation. Don't energise anything until the enclosure is closed.

---

## 1. What you need

**Hardware**
- ESP32-WROOM-32 dev board, DS3231 RTC module with a fresh CR2032 cell, and an 8-channel 5 V JD-VCC relay board (see the [README](../README.md#hardware) for the pin table)
- 5 V power supply rated **2 A or more**. The ESP32's WiFi peaks around 250 mA and each energised relay coil draws about 70–80 mA, with up to five on at once.
- A fault LED wired through relay 7's NC contact
- A USB data cable. Keep USB reachable in the enclosure: it's your recovery path if an over-the-air update ever goes wrong.
- A multimeter (continuity mode)

**Software and accounts**
- [PlatformIO](https://platformio.org/install), either the CLI or the VS Code extension
- Optional but recommended: a free [Healthchecks.io](https://healthchecks.io) account for offline alerts

**Information to collect first**
- WiFi name and password, **2.4 GHz** (the ESP32 has no 5 GHz radio)
- Your utility's peak hours, if they differ from MPEI 202.11 (17:00–22:00 Mon–Sat)
- Photos of the existing timer's wiring, with every wire labelled before you remove anything

---

## 2. Build and configure

### 2.1 Get the code

```bash
git clone https://github.com/divincentis/etsTimer.git
cd etsTimer
git checkout main        # once PR #1 is merged; until then:
                         # git checkout claude/repo-review-understanding-188tz8
```

### 2.2 Secrets

```bash
cp src/secrets.example.h src/secrets.h
```

Edit `src/secrets.h`. This file is git-ignored, so it never gets committed.

| Setting | Value |
|---------|-------|
| `WIFI_SSID` / `WIFI_PASSWORD` | Your 2.4 GHz network |
| `ADMIN_USERNAME` / `ADMIN_PASSWORD` | Login for `/admin`. Pick a real password. |
| `HEARTBEAT_URL` | Your Healthchecks ping URL (section 2.4), or leave `""` for now |

### 2.3 Review `src/config.h`

The defaults suit a Mountain Time MPEI customer. Check each of these:

| Setting | Default | Change it if… |
|---------|---------|---------------|
| `POSIX_TZ_STRING` | `MST7MDT,M3.2.0,M11.1.0` | You're not on Mountain Time. For example, Central is `CST6CDT,M3.2.0,M11.1.0`, Eastern is `EST5EDT,M3.2.0,M11.1.0`, and Saskatchewan (no DST) is `CST6`. |
| `RELAY_ON` / `RELAY_OFF` | `HIGH` / `LOW` | Your relay board is active-LOW. You'll find out in bench test 3.2. |
| `FAILSAFE_PEAK` | `false` (off-peak) | You want the heater held in peak mode while the clock is unknown. See section 4.2 first. |
| `PROTECT_CONTROLS` | `false` | You want the admin login required for setback and override too. |

### 2.4 Monitoring (recommended)

1. On Healthchecks.io, create a check with **Period 5 minutes** and **Grace 10 minutes**.
2. Add at least one notification channel (email, SMS, Telegram, ntfy…) and send a test notification.
3. Copy the ping URL (`https://hc-ping.com/…`) into `HEARTBEAT_URL`.

### 2.5 Build

```bash
pio test -e native       # unit tests on your computer — should end in SUCCESS
pio run                  # builds firmware; look for [SUCCESS] and the Flash % line
```

Don't use a `firmware.bin` from CI. It's built with placeholder WiFi credentials.

---

## 3. Bench test

Power the ESP32 and relay board **from USB only**, with nothing connected to the relay contacts.

### 3.1 First flash (must be USB)

This firmware uses a different partition table from the original, so the first install can't be done over the air.

```bash
pio run -t upload
pio device monitor       # 115200 baud; Ctrl+C to exit
```

If upload can't connect, hold the board's **BOOT** button while "Connecting…" is printed.

**Expected serial log** (each line starts with `[seconds since boot]`):

```
[0] Boot: Steffes CCRP Controller v2.1.0 (reset: power-on)
[0] Clock set from RTC: 2026-…            ← or "RTC lost power - …" on a brand-new RTC
[0] WiFi connecting to <your SSID>
[0] Web server started on port 80
[3] WiFi connected: 192.168.x.x (RSSI -55 dBm)
[3] NTP started (pool.ntp.org)
[4] NTP sync OK: 2026-…T…                 ← local time; check it's correct
```

Note the IP address. Tip: give the ESP32 a DHCP reservation on your router so the address never changes.

If you see `DS3231 RTC not found`, check the SDA→GPIO21 and SCL→GPIO22 wiring and power to the RTC.

### 3.2 Relay polarity ⚠ do this first

Open `http://etstimer.local` (or `http://<IP>`) and compare the **Relay Status** panel with the LEDs on the relay board.

- **Expected:** only RL7 (Watchdog) is on. The dashboard and the board's LEDs agree.
- **If the board shows the opposite** (RL1–RL6 lit, RL7 dark): your board is active-LOW. Swap `RELAY_ON`/`RELAY_OFF` in `config.h`, rebuild and re-flash, then check again.

Don't continue until the board and the dashboard agree.

### 3.3 Watchdog and fault LED

1. With the board running, RL7 is energised and the fault LED is **off**.
2. **Press and hold** the ESP32's **EN/RESET** button. That holds the ESP32 in reset exactly as a crash would: RL7 should release and the fault LED light for as long as you hold it.
3. Let go. The ESP32 boots, RL7 re-energises, and the LED goes off.

### 3.4 Clock and RTC

1. On the dashboard, the System card should show **Time source ✓ NTP** with your timezone abbreviation, and the clock should match your phone.
2. Turn your router's WiFi off (or change `WIFI_SSID` to a network that doesn't exist and re-flash), then press EN/RESET. The serial log should show `Clock set from RTC: …` with the correct local time. The schedule keeps running with no WiFi at all.
3. Restore WiFi (re-flash with the real SSID if you changed it). Within about 30 seconds the log shows `WiFi connected`, and the dashboard shows Time source ✓ NTP again.

### 3.5 Schedule switching

1. Open `/admin` and log in.
2. On **today**, add a peak window starting 2 minutes from now and lasting 3 minutes, then **Save Schedule**.
3. On the dashboard, watch the countdown reach zero. **RL1 should click on**, the status should change to **ON PEAK**, and the Activity log should show `Peak relay ON (peak)`. Three minutes later RL1 releases.
4. Check continuity with the multimeter across the RL1 contacts you plan to use, both energised and released. Note which of COM–NO or COM–NC closes in each state.
5. In `/admin`, click **MPEI default** (or enter your utility's hours) and **Save**. Make sure the preview matches your rate.

### 3.6 Setback and override

1. **Setback:** click Toggle, and RL2 switches. Press EN/RESET, and setback comes back in the same state, because it's saved.
2. **Override:** for each of the three zones, click **Send Override Pulse** and watch the board. The selector relay (RL4, RL5 or RL6) closes, then RL3 pulses for about half a second, then the selector opens. Only one selector should ever be on at a time.

### 3.7 Heartbeat

If you set `HEARTBEAT_URL`:

1. Within about a minute of WiFi connecting, the dashboard's **Heartbeat** row shows **✓** and the Healthchecks check turns **up**. The check's log shows the status line.
2. **Test the alert:** unplug the controller for 16 minutes (5-minute period + 10-minute grace + a margin). You should get a **down** notification. Plug it back in and you'll get **up** a minute or two after it boots.

If the Heartbeat row shows `✗ HTTP 404`, the URL is wrong. `✗ error -1` means it couldn't connect: check that the device has internet access and the correct time.

### 3.8 Bench sign-off

- [ ] Relay polarity matches the dashboard
- [ ] Fault LED is off when healthy and on when unpowered
- [ ] Time is correct and the source is NTP; the RTC keeps time without WiFi
- [ ] RL1 follows a test window, and you know which contacts close in the peak state
- [ ] Setback persists across reboot; the override sequence works for all three zones
- [ ] Heartbeat is up and an offline alert was actually received
- [ ] Final schedule saved and checked against your utility rate

---

## 4. Install

### 4.1 Before you start

- Turn the heater **off at its breaker**.
- Photograph the old timer's wiring and label each conductor with the terminal it came from.
- Check each control circuit's voltage and current against the **relay contact rating** printed on the relays. Don't assume it's low voltage.

### 4.2 Decide the failsafe wiring

When the ESP32 is dead or unpowered, **every relay is released**. Choose contacts so that released means the state you want during a failure:

| Relay | Released (ESP32 dead) should mean… | Why |
|-------|------------------------------------|-----|
| RL1 Peak | **Off-peak (charging allowed)**, which is the firmware's default failsafe | The house stays warm. The worst case is paying peak rate for a while, and the heartbeat tells you it's happening. |
| RL2 Setback | Normal (no setback) | Same reasoning |
| RL3–RL6 Override | No pulse, no zone selected | Released can never cause a pulse |
| RL7 Watchdog | Fault LED **on** (via NC) | That's its job |

Using the continuity results from bench test 3.5, connect to **COM–NO** or **COM–NC** so that "released" gives the state in the table **on your heater**. If your heater treats an open peak input as "peak" (charging blocked), wire RL1 through NC instead, and set `FAILSAFE_PEAK` to match what "time unknown" should do.

### 4.3 Mount and connect

1. Mount the enclosure where the ESP32 gets a decent WiFi signal. Check the RSSI on the dashboard first: better than −70 dBm is good, and −75 or worse shows as a warning.
2. Keep low-voltage control wiring and anything at mains voltage physically separated in the enclosure.
3. Land the control wires on the relay contacts chosen in 4.2, following your labels from the old timer.
4. Leave the USB port reachable.
5. Close the enclosure, restore the breaker, and power the controller.

---

## 5. Post-install verification

1. **Dashboard:** no warning banner, Time source ✓ NTP, RTC OK, Heartbeat ✓, and WiFi RSSI acceptable.
2. **Heater agrees:** the heater's own display or indicator shows the same peak/off-peak state as the dashboard.
3. **Watch the first real transition.** At 17:00 on a peak day, the dashboard flips to ON PEAK, the Activity log shows `Peak relay ON (peak)`, and the heater stops charging. At 22:00 it reverses. If you can't be there, check the Activity log afterwards: entries are timestamped.
4. **Setback and override:** exercise each once and confirm the heater responds as it did with the old timer.
5. **Power-cut test (optional):** turn the controller's supply off for 16 minutes. Healthchecks should alert. On power-up, the Activity log shows `Clock set from RTC` straight away and the schedule resumes before WiFi even connects.

Keep the old timer and your wiring photos until the controller has run a full week, so you can revert quickly.

---

## 6. Remote access

- **On your LAN:** `http://etstimer.local`. Some Android and older Windows devices don't resolve `.local`; use the IP address from your DHCP reservation instead.
- **From outside:** don't port-forward it. Run [Tailscale](https://tailscale.com/kb/1019/subnets) on an always-on machine on the same LAN (a Pi, NAS or router) as a **subnet router**. The ESP32 can't run Tailscale itself.

---

## 7. Updating firmware

Settings (schedule, setback) live in NVS and **survive updates**.

**Over the air (normal):**

1. `git pull`, then `pio test -e native && pio run`. Your local `secrets.h` is used.
2. Open `/admin` → **Firmware Update**, choose `.pio/build/esp32dev/firmware.bin`, and click **Upload & Install**.
3. The device reboots in a few seconds; relays drop out briefly. Check the new version under **Firmware** on the dashboard.

Updating during off-peak hours is best, so a brief drop-out doesn't matter.

**Over USB (fallback):** `pio run -t upload`. Use it if an update leaves the device unreachable, and for any change to the partition table.

There's no automatic rollback. If a new build boots but misbehaves, re-flash the previous version over USB: `git checkout <previous tag or commit> && pio run -t upload`.

---

## 8. Troubleshooting

| Symptom | Likely cause → fix |
|---------|--------------------|
| Relays are on when the dashboard says off | Active-LOW board. Swap `RELAY_ON`/`RELAY_OFF` and re-flash. |
| Banner: *clock not set — peak relay held in failsafe* | No NTP and the RTC time is invalid. Check WiFi and internet, then the RTC wiring and coin cell. |
| Banner: *RTC lost power* | Replace the CR2032. It clears after the next NTP sync. |
| Banner: *no NTP sync…* | NTP blocked or no internet. Check that the router allows outbound UDP 123. |
| Peak switches at the wrong hour | Wrong `POSIX_TZ_STRING`, or the schedule was entered for the wrong day. Check the dashboard clock and TZ abbreviation. |
| `etstimer.local` doesn't resolve | Use the IP address; mDNS isn't supported on every device. |
| Dashboard unreachable but relays still switching | Normal when WiFi is down: the schedule runs from the RTC. Check the router and the RSSI. |
| Heartbeat `✗ HTTP 404` | Wrong `HEARTBEAT_URL`. Copy it again from Healthchecks. |
| Heartbeat `✗ error -1` or other negative number | No internet, DNS failure, or TLS failure (check the clock is set). |
| *Last reset: task-watchdog / panic / brownout* | Watchdog or panic: note what was happening (Activity log) and report it. Brownout: the power supply is too weak or the cable too long. Use 5 V 2 A. |
| Can't log in to `/admin` | Wrong `ADMIN_*` in `secrets.h`. Re-flash over USB to change it. |
| OTA upload fails | Use a firmware built by `pio run` (not the ELF), check free flash in the build output, or fall back to USB. |

For more detail, `pio device monitor` shows the same Activity log messages with timestamps since boot.
