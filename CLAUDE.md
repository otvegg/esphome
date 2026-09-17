# esp32-screen-sensor

## Overview

An ESPHome-based ESP32 that drives a WeAct Studio 3.7" e-paper display as a Home
Assistant status dashboard. It's a "screen" node, not really a raw sensor node: it
mostly renders data pulled *from* Home Assistant (indoor temperature, weather
forecast, load-cell threshold counts from a companion device), plus a couple of
its own local sensors, onto the e-paper panel. How often it actually redraws,
and when it goes quiet overnight, are both adjustable live from Home
Assistant — see Data flow / integrations below.

## Project type & tooling

This is an **ESPHome** project — a YAML-driven firmware generator — not raw
PlatformIO/ESP-IDF/Arduino. There is no `platformio.ini` or `.ino`; the only
hand-written files are the ESPHome YAML config and one custom Python+C++
external component.

The `esphome` CLI is **not installed** in the local dev shell. It's presumably
run via Home Assistant's ESPHome add-on, a Python venv, or Docker elsewhere —
don't assume it's available when running commands.

## Repo layout

```
esp32-screensensor.yaml   # main device config — the actual project (this screen node)
esp32-sensorstation.yaml  # companion device config — kitchen_esp32_weight_sensor_station
                          # (its own separate ESP32; not flashed/managed by this same runtime)
secrets.yaml              # WiFi/API/OTA secrets — see Secrets section
.gitignore                # ignores /.esphome/ and /secrets.yaml
.esphome/                 # auto-generated build cache — never edit or commit
components/
└── uc8253_epaper/        # custom external component (display driver)
    ├── display.py        # ESPHome codegen/config schema
    ├── uc8253_epaper.h
    └── uc8253_epaper.cpp
```

`esp32-sensorstation.yaml` is kept here alongside the screen sensor's own
config for convenience since it's the data source for the load-cell
threshold counts (see Data flow / integrations below), but it targets a
separate physical ESP32 — flash/OTA/logs commands need to name it explicitly
(`esphome run esp32-sensorstation.yaml`, etc.), same as the screen sensor.

(Flattened from an earlier `esphome/` subdirectory layout — everything now
lives directly at the repo root.)

## Build / flash / logs

Standard ESPHome workflow (requires the `esphome` CLI to be available):

```
esphome run esp32-screensensor.yaml     # compile + flash
esphome compile esp32-screensensor.yaml
esphome upload esp32-screensensor.yaml  # OTA upload
esphome logs esp32-screensensor.yaml
```

There are no build scripts, Makefile, or CI — these commands are the whole
workflow.

## Hardware notes

- Board: `esp32dev`, framework: `arduino`.
- SPI on GPIO18 (CLK) / GPIO23 (MOSI) — deliberately moved off ESP32 defaults
  to free GPIO21/22 for I2C.
- I2C on GPIO21/22 (`scan: true`), used by the local AHT20 and BMP280 sensors.
- Display: WeAct Studio 3.7" black/white e-paper, 240×416 px, UC8253
  controller. ESPHome has no native driver for this panel, so
  `components/uc8253_epaper` is ported from the GxEPD2 Arduino library's
  `GDEY037T03` driver.
- The driver supports both the panel's **full-refresh** waveform
  (`update_full_()`, VCOM `0x97`) and its **partial-refresh** waveform
  (`update_part_()`, VCOM `0xD7`, wrapped in the `0x91`/`0x90`/`0x92`
  partial-window commands). Partial refresh is used for most updates since
  it doesn't flash the whole panel; a full refresh is forced periodically
  (`FULL_REFRESH_EVERY_N_UPDATES` in `uc8253_epaper.cpp`, currently every 30
  partial refreshes) to clear ghosting that partial waveforms leave behind
  over time, and always on the very first refresh after boot.
- After every physical refresh, the framebuffer is also written to the
  panel's "previous" memory (`0x10`) to keep it in sync with "current"
  (`0x13`) — partial refresh works by having the panel diff those two
  internally, so this must stay accurate or the diff (and thus which pixels
  it bothers to touch) is wrong. Ported from GxEPD2's `writeImageAgain()`.
- Buffer convention is inverted vs. the usual: `1 = white`, `0 = black`,
  matching the native panel format — see `draw_absolute_pixel_internal()`.
- `update()` diffs the framebuffer against a cached `prev_buffer_` and
  skips the physical refresh entirely when nothing changed, to avoid
  unnecessary flicker/wear.
- BUSY pin is active-low; watch for GPIO strapping pin conflicts if
  reassigning pins.

## Data flow / integrations

- Primary integration is the Home Assistant **native API** (encrypted, `api:`),
  auto-discovered via mDNS — **no MQTT** is used.
- Pulls from HA: indoor temperature, `weather.forecast_home` current +
  3-day forecast, and two load-cell weight sensors from a separate device
  (`kitchen_esp32_weight_sensor_station`) — used only to count daily
  threshold (500g) crossings, not displayed as raw weight.
- Local sensors (read directly by this ESP32 over I2C): AHT20
  (temperature/humidity) and BMP280 (temperature/pressure, addr `0x77`).
- `time:` via SNTP (Europe/Oslo) drives midnight resets of daily
  counters and daily min/max temperature.
- The display lambda also shows WiFi signal, uptime, an 8h rolling
  temperature graph, and a staleness warning if HA data hasn't updated
  recently.
- **Update interval and quiet hours are both HA-configurable**, via three
  `number`/`datetime` template entities exposed to Home Assistant
  (`entity_category: config`, so they show up under the device's
  Configuration section, not as regular sensors):
  - **"Update Interval"** (`update_interval_minutes`, 1–30 min, default 5) —
    how often the display actually redraws outside quiet hours. The
    underlying ESPHome `update_interval: 60s` on the display component is
    just the poll tick; the lambda itself skips drawing (and thus the
    physical refresh) unless at least this many minutes have passed since
    `last_draw_ms`, using the same frozen-buffer trick as quiet hours below.
  - **"Quiet Hours Start"/"Quiet Hours End"** (`quiet_hours_start`/`_end`,
    default 02:00/05:30) — during this window the display freezes
    completely instead of redrawing on any cadence. The window comparison
    handles crossing midnight (e.g. 23:00→06:00), not just same-day ranges.
  - All three are plain `optimistic: true` + `restore_value: true` templates
    (no `lambda`/`set_action`) — Home Assistant's set command is the only
    thing that ever changes them, and the chosen values survive a reboot.
- Freezing (both quiet hours and the interval throttle) works via
  `auto_clear_enabled: false` plus an early `return` in the lambda, which
  leaves the framebuffer byte-identical to last time — the driver's
  unchanged-buffer check in `update()` then skips the physical refresh too.
  The `graph:` component keeps sampling `my_sensor` in the background
  regardless (it hooks the sensor's value updates, not the display's refresh
  cycle), so the graph has no gap when the display redraws again. The one
  frame drawn right as quiet hours begin adds a moon icon + "Frozen HH:MM"
  notice so it's clear overnight that the screen isn't live, not that HA has
  gone stale.

## Secrets & safety

`secrets.yaml` holds real WiFi credentials, the HA API encryption
key, and the OTA password. **Never commit this file.** It's already listed
in `.gitignore`, but note that **this directory is not yet a git
repository** — if you run `git init` here, double-check `secrets.yaml` is
excluded before the first commit. The OTA password is currently set to the
placeholder `"changeme"` and should be replaced with a real secret.

## Testing

There are no automated tests in this project. Verification is done by
flashing the device and checking the rendered display / HA logs.
