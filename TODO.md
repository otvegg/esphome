# TODO

- [ ] Flash/validate the recent changes (partial refresh, HA-configurable
      update interval + quiet hours) — run `esphome config` then
      `esphome run esp32-screensensor.yaml`; nothing since the
      original display driver has actually been tested on hardware yet.
- [ ] Replace the placeholder OTA password (`"changeme"` in
      `secrets.yaml`) with a real secret.
- [ ] Initialize git for this project (`git init`), with `.gitignore`
      covering `.venv/`, `.esphome/`, and `secrets.yaml`
      before the first commit.
- [ ] Add `.venv/` to `.gitignore` (created for the local ESPHome install,
      not yet excluded anywhere).
- [ ] Reconsider MQTT for the load-cell weight readings (esp32-sensorstation
      publishing, esp32-screensensor subscribing directly) instead of the
      current pure HA-based path. This was tried and reverted: the real
      benefit is only against HA Core restarts (fairly common, e.g. after
      updates), not against the whole server/Supervisor going down, since
      Mosquitto would typically run on the same machine — so it protects a
      narrower case than a full outage. Worth revisiting if HA restarts turn
      out to cause noticeable gaps in the weight/threshold-count data, or if
      a Mosquitto broker ends up needed for other integrations anyway.
