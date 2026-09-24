# Sensor station: battery + deep sleep plan

Goal: run `esp32-sensorstation.yaml` (ESP32 DevKit + 2× HX711 load cells) on
an 18650 for months instead of on a USB power bank, using ESP32 deep sleep.

## Why the current setup can't just "add deep sleep"

- **The power bank will shut off.** Most power banks cut out below
  ~50–100 mA. A sleeping ESP32 draws far less, so the bank turns off.
- **The DevKit isn't low-power when fed over USB.** Its AMS1117 regulator
  (~5 mA idle), USB-serial chip and power LED stay on while the ESP32
  sleeps.
- **The firmware assumes it's always on.**
  - The boot tare would zero the scale on every wake, even with an item on
    it.
  - `throttle: 30s` publishes the first (unsettled) reading and then blocks
    the settled one.

## Target design

**Power path.** Replaces the power bank: same cell and charger, minus the
5 V booster and the auto-off chip.

```
18650 ── TP4056 (B+/B−) ── OUT+/OUT− ── HT7833/RT9080 ── DevKit 3V3 + GND
                                                          └── HX711 ×2 VCC
```

- **Feed the DevKit on its 3V3 pin, not VIN/5V.** VIN goes through the
  onboard AMS1117, which needs ~4.4 V in and draws ~5 mA idle.
- **Don't connect the cell straight to 3V3.** A charged 18650 is at 4.2 V,
  and the ESP32's maximum is 3.6 V.
- **Unplug the battery side before connecting USB for flashing.**
  Otherwise two regulators drive 3V3 at the same time. Charging through the
  TP4056's own USB port is fine.

**Wake-up**
- **SW-18010P vibration switch.** One per platform, wired in parallel
  between **GPIO33** and GND. The ESP32's internal RTC pull-up holds the pin
  during sleep, so no external resistor is needed.
  - It only reacts to movement: placing or removing an item wakes the
    ESP32. A weight sitting still doesn't hold the switch closed, so it goes
    back to sleep.
- **Timer wake every 2 h** as a heartbeat, which also catches any placement
  too gentle to trigger the switch.

**HX711 power-down.** No MOSFETs needed.
- Holding PD_SCK high for more than 60 µs puts the HX711 into power-down
  (under 1 µA). This also cuts the load cell's excitation current.
- The firmware drives both SCK pins high before sleeping and latches them
  with `gpio_hold_en`. On wake it calls `gpio_hold_dis` *before* the hx711
  component starts. That hold only works on RTC-capable pins, so:
  - **Load cell 1 SCK moves GPIO17 → GPIO26.**
  - Load cell 2 SCK stays on GPIO27, which is already RTC-capable.

**Pin map (after changes)**

| Function | GPIO |
|---|---|
| Load cell 1 DOUT | 16 |
| Load cell 1 SCK | **26** (was 17) |
| Load cell 2 DOUT | 4 |
| Load cell 2 SCK | 27 |
| Vibration switch wake | 33 |
| Onboard blue LED (driven low and held in sleep) | 2 |

## Firmware changes (`esp32-sensorstation.yaml`)

- **Remove** AHT20, BMP280 and `i2c:`. Another device already covers these.
  Check that no HA dashboard or automation uses their entities.
- **Keep the tare across deep sleep.**
  - The tare offsets go in `RTC_DATA_ATTR` variables, via a small header
    added through `esphome: includes:`.
  - The tare only runs on a real power-up, when the RTC memory is blank.
  - A **"Tare" button** entity lets you re-zero it manually.
- **Wake cycle script.** Replaces the `throttle: 30s` behaviour:
  1. Wait ~500 ms for the HX711 to settle after power-down.
  2. Wait until the last ~6 readings are within ±2 g (8 s timeout).
  3. Publish each cell's weight once.
  4. Wait for the HA API connection (15 s timeout), plus ~1 s.
  5. Enter deep sleep, unless the stay-awake switch is on.
- **Split the load-cell sensors.**
  - The hx711 sensors become internal and only feed the stability check.
  - Template sensors named "Load Cell 1"/"Load Cell 2" hold the published
    values. They keep the same names, so the existing HA entities
    (`sensor.kitchen_esp32_weight_sensor_station_load_cell_*`) stay the
    same.
- **`deep_sleep:`**
  - `sleep_duration: 2h` (1–2 min while testing).
  - `wakeup_pin: GPIO33` (inverted), `wakeup_pin_mode: KEEP_AWAKE`.
  - `on_shutdown`:
    - drives SCK pins high and holds them
    - enables the GPIO33 RTC pull-up
    - drives GPIO2 low and holds it (blue LED off)
- **Stay awake for OTA.**
  - A `homeassistant` binary sensor reads
    `input_boolean.sensorstation_stay_awake`.
  - The station stays awake while it's on, and sleeps as soon as it's
    switched off.
- **Speed up each wake.**
  - `wifi: fast_connect: true`, `power_save_mode: none`, `manual_ip:`
    (skips DHCP).
  - `api: reboot_timeout: 0s`.
- **Remove `ap:` and `captive_portal:`.** The fallback hotspot only starts
  after ~1 min awake, which never happens now. Recovery is done by flashing
  over USB serial.

## Screen node change (`esp32-screensensor.yaml`)

- **Add a NaN guard to both >500 g rising-edge counters.** If the station
  ever shows up as unavailable, `was_above` must not reset. Otherwise an
  item still sitting on the scale gets counted a second time.

## Shopping list

Already have: DevKit, TP4056 (6-pin, with protection), 18650 box, HX711s,
jumper wires.

| Item | Qty | Notes |
|---|---|---|
| SW-18010P vibration switch | 1 per platform | Smallest pack available |
| HT7833 or RT9080-33 3.3 V LDO, **breakout module with caps** | 1 | Low idle current, handles WiFi spikes. Not HT7333/MCP1700 (only 250 mA) |
| 18650 cell | 1 | Only if none on hand. The power bank's cells work |

Only if needed later:
- **A 470 µF capacitor on 3V3**, if the ESP32 resets when WiFi connects.
- **100 nF across the vibration switch**, if short taps get missed.
- **A multimeter with a µA range**, for verifying the sleep current.

## Steps

### Step 1: firmware and testing (no new parts)

- [ ] Write firmware changes above (sleep_duration 1–2 min for testing)
- [ ] Add NaN guard to screen node counters
- [ ] Create HA helper: Settings → Devices & services → Helpers → Toggle,
      `sensorstation_stay_awake`
- [ ] Move load cell 1 SCK wire from GPIO17 to GPIO26
- [ ] Flash over USB, powered from a PC or phone charger (**not** the power
      bank, which shuts off once the ESP32 sleeps)
- [ ] Verify: wakes → publishes weight in HA → sleeps
- [ ] Verify: an item left on the scale still reads correctly after several
      wakes (tare survived sleep)
- [ ] Verify: stay-awake toggle keeps it up long enough for an OTA update
- [ ] Verify: screen node's >500 g counts still behave

### Step 2: hardware, when parts arrive

- [ ] **Check whether the 18650 box is wired in series.** Look for a link
      from one slot's + to the other's −, or measure ~8 V across the leads.
      Series would destroy the TP4056; if so, use a single cell.
- [ ] Wire 18650 → TP4056 → LDO → DevKit 3V3/GND; HX711 VCC from 3V3
- [ ] Mount SW-18010P firmly on the platform or frame (not the table).
      Wire it GPIO33 ↔ GND and try a couple of orientations for sensitivity.
- [ ] Set `sleep_duration: 2h` and flash
- [ ] Check that WiFi connects without resets. If not, add a 470 µF on 3V3.

### Step 3: measure and tune

- [ ] Measure sleep current in series with the battery. Target: well under
      1 mA. Expect roughly 10–100 µA once the LEDs are dealt with.
- [ ] If it's too high, deal with the red power LED. It's hardwired, so
      desolder it or its resistor, or cut its trace. If holding GPIO2 low
      still draws current, remove the blue LED too.
- [ ] Optional: start with WiFi off (`wifi: enable_on_boot: false`) and only
      connect when the weight changed by more than a few grams. That makes
      false wakes (bumps, cupboard doors) much cheaper.

## Expected battery life

Rough estimate: ~20 place/remove events a day, ~5 s awake at ~100 mA per
wake, and 10–100 µA while asleep. That works out to about **a year on one
18650**, versus ~1.5–2 days today. The real figure depends on the measured
sleep current.
