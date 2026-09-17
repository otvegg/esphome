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
