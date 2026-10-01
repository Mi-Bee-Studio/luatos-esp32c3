# Sense Node User Guide

[中文文档](user-guide.zh.md) | [English](user-guide.md)

**Applicable firmware:** 0.1.0+ | **Hardware:** ESP32-C3 + Luatos Air101 LCD (ST7735S 160×80) + 5 buttons

This manual is written for everyday users. For the project's positioning, see
[README.md](../README.md).

---

## 1. What Is This

A **camera-free** Wi-Fi sensing terminal: it uses the channel disturbance (CSI) of
Wi-Fi probe frames to tell **whether anyone is in the room, whether they are moving,
and roughly how many breaths per minute they take**, with the results shown on the
small on-device screen. The precise analysis is done by the homepulse engine on the
PC side (the home sensing platform), connected over serialtap (the USB-serial
middleware) — see Section 7.

```
Sense node (this device) ──USB──> serialtap (logging + pass-through) ──> homepulse (PC engine) ──status returned──> mirrored on the mini screen
```

CSI only reflects amplitude/phase disturbance of the wireless channel and **contains
no image or audio content whatsoever**; it cannot reconstruct the details of specific
movements either — all it knows is "the channel changed". Privacy-friendly.

## 2. Power-On and Boot Sequence

1. After plugging in USB, the screen lights up with the **SENSE NODE** boot splash (about 2 seconds);
2. Automatically enters the **Sense page**;
3. If Wi-Fi credentials are stored, it connects automatically (retrying every 5 seconds on failure);
4. Once online, CSI collection starts automatically; if the PC engine is online, it enters tracking after a 30-second environment calibration.

## 3. The Three Pages

**The LEFT / RIGHT buttons cycle through the three pages (Sense -> NET -> SYS).**

### Sense Page (default, the main sensing page)

| Area | Meaning |
|------|---------|
| Status card (border color follows state) | `* PRESENT` someone present (green) / `- ABSENT` no one (gray) / `CALIBRATING` calibrating (yellow) / `* PAUSED` streaming paused (yellow, press UP to resume) / `CAL IN Ns` delayed-calibration countdown (yellow, CENTER long-press) / `NO SIGNAL` no CSI (not on the network) / `WAIT PC` collecting but the PC engine has not connected |
| `BR 15.3 Q72%` | Breathing rate and confidence (**a precise value only exists while the PC engine is online**; `BR -- local mode` = local coarse-estimate fallback; `unstable` = signal too weak, gated off; while armed the row shows `needs empty room`) |
| `MOT ▓▓▓░ 42 walk` | Motion intensity bar and level (idle = still / light = slight movement / walk = walking) |
| Right side of the title bar | `21Hz -45 PC` = sampling rate / signal strength (dBm) / data comes from the PC (**green when PC, yellow in local mode, gray with no data**); dots next to the page name = current page indicator (three pages) |
| Bottom overlay | Action toast: every button action pops a confirmation for 1.5 s (e.g. `STREAM OFF`, `CAL IN 10s`) |

### NET Page (network & link, new in v2.3)

| Row | Meaning |
|-----|---------|
| `WIFI SSID` | Wi-Fi name (full string shown, ellipsis if longer; `--` = disconnected, reconnecting) |
| `IP 192.168.x.x` | This device's address (the web maintenance page :80 lives here) |
| `LINK PC` / `LOC` | Data source: PC engine online (green) / local autonomy (yellow) |
| `STRM ON/OFF` (right-aligned) | CSI streaming state (toggles with UP) |
| `RSSI -47 dBm` | Signal strength |

### SYS Page (system health)

A two-column grid plus a shortcut hint line at the bottom:

| Row | Meaning |
|-----|---------|
| `UP 3h12m  HEAP 142K` | Uptime / remaining heap |
| `CSI 123k dr45` + `v0.1.0` (row end) | Cumulative CSI packet count / queue drop count (`dr` only needs attention if it keeps growing) / firmware version |
| `FW v0.1.0  ESP32-C3` | Firmware version and chip |
| Bottom dim line | Five-button cheat sheet: `L/R PAGE U STRM D DIAG L+R LCD` |

## 4. Buttons (five keys + long-press + combo)

| Button | Action |
|--------|--------|
| LEFT / RIGHT | Cycle Sense -> NET -> SYS pages |
| UP | **Pause / resume CSI streaming** (global, works on any page). While paused, the Sense page shows `* PAUSED` in large text, stimulus pings stop, and the homepulse panel shows the data stopping; press UP again to resume (reusing the previous sampling parameters). Useful for temporarily silencing the device, or for verifying PC-side link-loss handling |
| DOWN | **Diagnostic snapshot**: writes the current sensing state and CSI counters as one log line into serialtap's log stream (a way to "place a marker" on the timeline when troubleshooting on site) |
| CENTER short press (Sense page) | **Recalibrate** (re-learns the 30-second environment baseline, see Section 5) |
| CENTER short press (NET / SYS pages) | Diagnostic snapshot (same as DOWN) |
| CENTER **long press** (~0.7 s, all pages) | **Arm a 10-second delayed calibration**: the countdown shows on the Sense page status card; calibration starts automatically when it reaches zero — press, then leave the room. Long-press again while armed to **cancel** |
| LEFT + RIGHT **held together** (≥1 s) | **Screen off** (display off, sensing unaffected); any key **wakes** it. **It also turns itself off after 10 idle minutes** (anti burn-in), then peeks randomly: every 1-5 min it lights for 1-60 s showing the live page or a small expression animation; any key wakes |

Every button action gets a toast receipt at the bottom of the screen (1.5 s),
so what you pressed is always visible.

> While paused, homepulse will show the device as offline / no data because the data
> stream has stopped — this is expected behavior, not a fault;
> data returns to the panel within 1–2 seconds after streaming resumes.

## 5. Calibration

Sensing thresholds depend on the environment (room size, furniture, the relative
positions of people and the board). **Recalibration is recommended after moving house,
changing the power socket location, or major furniture changes**: press CENTER on the
Sense page, or click "Recalibrate" in the homepulse panel / tray menu. During
calibration the screen shows a yellow `CALIBRATING`; it takes about 30 seconds, and
**nobody should be moving around the room during that time** (sitting still is fine).
An automatic calibration also runs at every power-up.

**How can someone sitting at the computer leave the room and still trigger calibration?**
Three "arm-and-leave" paths — pick any: **CENTER long press** (on the board itself:
a 10-second countdown shows on screen, long-press again to cancel), or
**"Calibrate after leaving (60 s)"** in the homepulse panel / tray menu or the
on-device Web (Section 6a) — click it and walk away; calibration starts
automatically once the countdown ends. Click it again to cancel if you change
your mind. The biggest advantage of the on-device Web is that **it works from a phone**:
you can tap it even after you have already left the room.

## 6a. On-Device Web (direct control from a phone / browser)

Once Wi-Fi is connected, the board itself becomes a small website: open
**`http://<board-IP>/`** in a browser (the IP is shown on the LCD **NET page**);
any phone on the same Wi-Fi can access it. What it can do:

- **View status**: presence/absence, motion, breathing, sampling rate, signal; auto-refreshes every 2 seconds
- **Calibrate**: calibrate now / calibrate after leaving (60-second arming, cancellable) — **tap it on your phone from outside the room**, which fully resolves the paradox "calibration needs the room empty, but the button needs someone present"
- **Wi-Fi provisioning**: scan nearby networks → pick one → enter the password → connect (no USB needed to switch networks)

Note: the on-device Web has no password; use it only inside your home intranet.
If you ever expose it to the public internet, add reverse-proxy authentication yourself first.

## 6. Wi-Fi Provisioning

Credentials are stored in the board's NVS; switching networks takes just one JSON
command. The easiest routes: the **"Device Settings · Wi-Fi Provisioning" area of the
homepulse panel** (click scan, pick a network, enter the password — that's it), or the
**on-device Web** (Section 6a). The manual methods:

**Method A — serialtap proxy (recommended; no need to unplug the board)**

```bash
# 1. Open a pass-through endpoint (the daemon must already be running)
serialtap proxy esp32            # note the printed address, e.g. 127.0.0.1:59085

# 2. Send the provisioning command (bash example)
exec 3<>/dev/tcp/127.0.0.1/59085
printf '{"cmd":"wifi_connect","ssid":"YourWiFiName","pass":"YourPassword"}\n' >&3
# Optional: scan nearby APs first
printf '{"cmd":"wifi_scan"}\n' >&3
```

**Method B — any serial terminal**: open the board's serial port at 115200 baud and paste the JSON line above directly.

Check the connection result on the NET page or in the serial log (`Got IP:` means success).

## 7. Setting Up with a PC (getting the full stack running)

```bash
# Terminal 1: the serialtap daemon (USB serial logging + pass-through proxy + tray)
serialtap run

# Terminal 2: the homepulse sensing engine (auto-discovers the device; the device name is a regex)
homepulse run -device esp32
```

- Browser observation: the serialtap panel at `http://127.0.0.1:8801/` (raw serial
  log / event stream) and the homepulse panel at `http://127.0.0.1:8799/`
  (sensing status + 3-minute curves).
- The system tray icons of both reflect the sensing state by color (gray = disconnected /
  no data, amber = calibrating, blue = no one present, green = someone present).
- If you just want the board to run standalone (no PC): install nothing — the board
  automatically enters local mode with limited functionality (coarse presence
  estimate only, no precise breathing rate).

## 8. Firmware Upgrade

```bash
serialtap flash esp32 \
  build/bootloader/bootloader.bin@0x0 \
  build/partition_table/partition-table.bin@0x8000 \
  build/esp32c3_info_display.bin@0x10000
```

serialtap automatically pauses collection → invokes esptool → resumes automatically
after flashing; the whole process is visible in the panel's event stream.
The homepulse session is briefly disconnected during flashing — that is expected;
it reconnects automatically afterwards.

## 9. Troubleshooting

| Symptom | Cause and handling |
|---------|--------------------|
| Large text `NO SIGNAL` | Not on the network, so no CSI to speak of. Check the Wi-Fi row on the NET page; re-provision (Section 6) |
| `WAIT PC` stays on indefinitely | The board is collecting but homepulse has not connected. Confirm homepulse is running and that `-device` matches the device name (serialtap `status` shows it) |
| `BR -- local mode` | The normal local fallback. Start homepulse if you want precise breathing values |
| `BR -- (unstable)` | Someone is moving, or the sampling rate is insufficient. Stay still for 30 seconds and check again; confirm the homepulse config `stimulus_hz: 20` |
| Serial log silent for 1–3 minutes, then recovers by itself | Known behavior: a transient USB CDC stall on the single-core ESP32-C3 under CSI + display load. The board recovers on its own; not a freeze |
| Wi-Fi reconnects over and over | Check the reason code in the log: 202 = AP not found (wrong name / 5 GHz band? The C3 only supports 2.4 GHz), 201 = wrong password |
| The `dr` figure on the SYS page climbing fast | CSI queue overflow, usually because the serial port has been grabbed by an external tool and the data cannot flow out; check whether another program has the serial port open directly |
| Screen misaligned / rows missing | Since v2.0 there is a compile-time overflow-prevention assertion; if you still hit this, please file a bug attaching the `UI_COMMON: row overflow` line from the serial log |

## 10. Placement and Effective Range (field-tested experience)

- **Walk detection**: house-wide scale (basically covers anywhere in the same room;
  avoid a load-bearing wall between the board and the router);
- **Light motions (waving a hand / standing up)**: reliable within 1–2 m;
- **Stationary breathing**: the most sensitive regime; recommended within 2 m, with
  the board and the person on the same side as the router;
- If the board is too far from the router (RSSI < -70dBm), sampling quality drops
  noticeably; prioritize the board–router link quality first.

---
*This manual is based on firmware v2.0; for changes to protocol details, see the CHANGELOG/README of the two repositories.*
