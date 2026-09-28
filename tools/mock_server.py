#!/usr/bin/env python3
"""Fake ETS controller for working on the web UI without hardware.

    python3 tools/mock_server.py            # then open http://localhost:8080
    python3 tools/mock_server.py 8080 "RTC lost power - replace coin cell?"   # simulate a problem

Serves data/www/* and mimics the firmware's JSON API (no auth).
"""

import json
import sys
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parent.parent / "data" / "www"
TZ = ZoneInfo("America/Denver")
DAYS = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]
BOOT = time.time()

state = {
    "schedule": {d: ([{"start": "17:00", "end": "22:00"}] if d not in ("sun",) else []) for d in DAYS},
    "setback": False,
    "override_until": 0.0,
    "last_zone": -1,
    "events": [],
    "problems": sys.argv[2:],  # e.g. mock_server.py 8080 "RTC lost power - replace coin cell?"
}


def log(text):
    state["events"].insert(0, {"epoch": int(time.time()), "uptime": int(time.time() - BOOT), "text": text})
    del state["events"][40:]


def to_min(hhmm):
    h, m = hhmm.split(":")
    return int(h) * 60 + int(m)


def is_peak(dow, minute):
    for w in state["schedule"][DAYS[dow]]:
        s, e = to_min(w["start"]), to_min(w["end"])
        if (minute >= s) if e < s else (s <= minute < e):
            return True
    for w in state["schedule"][DAYS[(dow + 6) % 7]]:
        s, e = to_min(w["start"]), to_min(w["end"])
        if e < s and minute < e:
            return True
    return False


def minutes_until_change(dow, minute):
    cur = is_peak(dow, minute)
    t = dow * 1440 + minute
    for step in range(1, 7 * 1440 + 1):
        u = (t + step) % (7 * 1440)
        if is_peak(u // 1440, u % 1440) != cur:
            return step
    return -1


def status():
    now = datetime.now(TZ)
    dow = (now.weekday() + 1) % 7  # Python: Monday=0 → tm_wday: Sunday=0
    minute = now.hour * 60 + now.minute
    peak = is_peak(dow, minute)
    busy = time.time() < state["override_until"]
    relays = [
        ("peak", "Peak", peak),
        ("setback", "Setback", state["setback"]),
        ("override", "Override", busy),
        ("selRl1", "Sel RL1", busy and state["last_zone"] == 0),
        ("selRl23", "Sel RL2,3", busy and state["last_zone"] == 1),
        ("selRl789", "Sel RL7-9", busy and state["last_zone"] == 2),
        ("watchdog", "Watchdog", True),
    ]
    return {
        "device": "Steffes CCRP Controller (mock)",
        "firmware": "2.1.0",
        "uptime": int(time.time() - BOOT),
        "reset": "power-on",
        "time": {
            "valid": True, "source": "ntp", "local": now.strftime("%Y-%m-%dT%H:%M:%S"),
            "tz": now.strftime("%Z"), "dow": dow, "minute": minute, "second": now.second,
            "ntpAge": int(time.time() - BOOT) % 3600,
        },
        "peak": {"active": peak, "failsafe": False, "minutesUntilChange": minutes_until_change(dow, minute)},
        "setback": state["setback"],
        "override": {"busy": busy, "lastZone": state["last_zone"], "zones": ["RL1", "RL2, RL3", "RL7, RL8, RL9"]},
        "relays": [{"id": i, "label": l, "on": on} for i, l, on in relays],
        "rtc": {"present": True, "lostPower": False, "tempC": 23.75},
        "wifi": {"connected": True, "ssid": "mock-net", "rssi": -58, "ip": "127.0.0.1", "hostname": "etstimer"},
        "problems": list(state["problems"]),
        "heartbeat": {"enabled": True, "interval": 300, "lastOkAge": 42, "lastTryAge": 42,
                      "lastCode": 200, "reportedFail": bool(state["problems"])},
    }


class Handler(BaseHTTPRequestHandler):
    def send_json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_file(self, path):
        body = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            return self.send_file(ROOT / "index.html")
        if path == "/admin" or path.startswith("/admin/"):
            return self.send_file(ROOT / "admin" / "schedule.html")
        if path == "/api/status":
            return self.send_json(200, status())
        if path == "/api/schedule":
            return self.send_json(200, {"days": state["schedule"]})
        if path == "/api/health":
            ok = not state["problems"]
            return self.send_json(200 if ok else 503, {"ok": ok, "problems": state["problems"]})
        if path == "/api/events":
            return self.send_json(200, state["events"])
        self.send_error(404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length)

        if self.path == "/admin/api/firmware":
            if not self.headers.get("X-ETS-Request"):
                return self.send_json(403, {"ok": False, "error": "missing X-ETS-Request header"})
            log(f"Firmware upload received ({len(raw)} bytes) - mock does nothing")
            return self.send_json(200, {"ok": True})

        # Like the firmware, JSON endpoints only accept application/json.
        if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
            return self.send_error(404)
        body = json.loads(raw or b"{}")

        if self.path == "/api/setback":
            if not isinstance(body.get("on"), bool):
                return self.send_json(400, {"ok": False, "error": "expected {\"on\": true|false}"})
            if body["on"] != state["setback"]:
                state["setback"] = body["on"]
                log(f"Setback {'ON' if body['on'] else 'OFF'}")
            return self.send_json(200, {"ok": True})

        if self.path == "/api/override":
            zone = body.get("zone")
            if zone not in (0, 1, 2):
                return self.send_json(400, {"ok": False, "error": "zone must be 0, 1 or 2"})
            if time.time() < state["override_until"]:
                return self.send_json(409, {"ok": False, "error": "override already in progress"})
            state["override_until"] = time.time() + 0.6
            state["last_zone"] = zone
            log(f"Override pulse: zone {['RL1', 'RL2, RL3', 'RL7, RL8, RL9'][zone]}")
            return self.send_json(200, {"ok": True})

        if self.path == "/admin/api/schedule":
            days = body.get("days")
            if not isinstance(days, dict) or any(not isinstance(days.get(d), list) for d in DAYS):
                return self.send_json(400, {"ok": False, "error": "every day (sun..sat) must be present as an array"})
            for d in DAYS:
                if len(days[d]) > 4:
                    return self.send_json(400, {"ok": False, "error": "too many windows in a day (max 4)"})
                for w in days[d]:
                    if w.get("start") == w.get("end"):
                        return self.send_json(400, {"ok": False, "error": "window start and end are equal"})
            state["schedule"] = {d: days[d] for d in DAYS}
            log("Schedule updated")
            return self.send_json(200, {"ok": True})

        if self.path == "/admin/api/reboot":
            log("Reboot requested from web")
            return self.send_json(200, {"ok": True})

        self.send_error(404)

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    log("Boot: mock controller")
    log("NTP sync OK")
    print(f"Mock ETS controller on http://localhost:{port}")
    ThreadingHTTPServer(("", port), Handler).serve_forever()
