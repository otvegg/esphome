# esphome

A small collection of ESPHome-based ESP32 devices for the smarthome. The main
one drives a WeAct Studio 3.7" e-paper display as a Home Assistant status
dashboard — a "screen" node that renders indoor/outdoor temperature, a
3-day forecast, and load-cell weight-threshold counts, plus a temperature
and weight graph, onto the e-paper panel.

The companion device, `esp32-sensorstation.yaml`, is a separate physical
ESP32 (a kitchen load-cell weight sensor station) kept in this same repo for
convenience.

## Project type & tooling

This is an **ESPHome** project — a YAML-driven firmware generator — not raw
PlatformIO/ESP-IDF/Arduino. There is no `platformio.ini` or `.ino`; the only
hand-written files are the ESPHome YAML configs and one custom Python+C++
external component.

The `esphome` CLI runs from a local Python venv (`.venv/`, gitignored) —
`python3 -m venv .venv && .venv/bin/pip install esphome`.

## Build / flash / logs

Run against whichever device's YAML file you're targeting
(`esp32-screensensor.yaml` or `esp32-sensorstation.yaml`):

```
esphome run <file>.yaml       # compile + flash (asks serial vs. OTA)
esphome compile <file>.yaml   # compile only, no flashing
esphome upload <file>.yaml    # flash over OTA (device must already be on WiFi)
esphome logs <file>.yaml      # attach to the running device's log stream only —
                               # does not compile or flash anything
```

## Hardware notes

- Board: `esp32dev`, framework: `arduino`.
- SPI on GPIO18 (CLK) / GPIO23 (MOSI) — moved off ESP32 defaults to free
  GPIO21/22 for I2C.
- I2C on GPIO21/22 (`scan: true`), used by the local AHT20 and BMP280 sensors.
- Display: WeAct Studio 3.7" black/white e-paper, 240×416 px, UC8253
  controller — no native ESPHome driver, so `components/uc8253_epaper` is
  ported from GxEPD2's `GDEY037T03` driver, supporting both full and partial
  refresh.

## Data flow / integrations

- Primary integration is the Home Assistant native API (encrypted, `api:`) —
  **no MQTT** is used (see TODO below).
- Pulls from HA: `weather.forecast_home` current + 3-day forecast, and two
  load-cell weight sensors from the sensor station — used to count daily
  threshold (500g) crossings and plot a weight trend.
- Local sensors (read directly over I2C): AHT20 (temperature/humidity,
  used for the temp graph) and BMP280 (temperature/pressure).
- **Update interval and quiet hours are HA-configurable**, via `number`/
  `datetime` template entities under the device's Configuration section:
  update cadence (1–30 min, default 5), and a quiet-hours window (default
  02:00–05:30) during which the display freezes instead of redrawing.

## Secrets & safety

`secrets.yaml` holds real WiFi credentials, the HA API encryption key, and
the OTA password. **Never commit this file** — it's gitignored. The OTA
password is currently the placeholder `"changeme"` and should be replaced
with a real secret (see TODO below).

## Testing

There are no automated tests. Verification is done by flashing the device
and checking the rendered display / HA logs.

## TODO

- [ ] Replace the placeholder OTA password (in
      `secrets.yaml`) with a real secret.
- [ ] Reconsider MQTT for the load-cell weight readings (esp32-sensorstation
      publishing, esp32-screensensor subscribing directly) instead of the
      current pure HA-based path. This was tried and reverted: the real
      benefit is only against HA Core restarts (fairly common, e.g. after
      updates), not against the whole server/Supervisor going down, since
      Mosquitto would typically run on the same machine — so it protects a
      narrower case than a full outage. Worth revisiting if HA restarts turn
      out to cause noticeable gaps in the weight/threshold-count data, or if
      a Mosquitto broker ends up needed for other integrations anyway.
