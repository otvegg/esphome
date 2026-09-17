#pragma once

// Custom ESPHome display driver for the WeAct Studio 3.7" black/white e-paper
// panel (240x416, UC8253 controller). Not natively supported by ESPHome as of
// 2026.x (no waveshare_epaper model, no epaper_spi model).
//
// The command sequence and register values below are ported directly from
// the GDEY037T03 driver in the GxEPD2 Arduino library
// (https://github.com/ZinggJM/GxEPD2, src/gdey/GxEPD2_370_GDEY037T03.cpp),
// which is itself based on Good Display's vendor demo code for this panel.
// Only the plain (slow) full-refresh path is implemented - no fast-refresh
// or partial-refresh support - to keep the register sequence identical to
// the simplest, most-likely-to-work path in the reference driver.

#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/display/display_buffer.h"

namespace esphome {
namespace uc8253_epaper {

class UC8253EPaper : public display::DisplayBuffer,
                      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                             spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_4MHZ> {
 public:
  void set_dc_pin(GPIOPin *dc_pin) { this->dc_pin_ = dc_pin; }
  void set_reset_pin(GPIOPin *reset) { this->reset_pin_ = reset; }
  void set_busy_pin(GPIOPin *busy) { this->busy_pin_ = busy; }

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_BINARY; }

 protected:
  static const int WIDTH = 240;
  static const int HEIGHT = 416;

  int get_width_internal() override { return WIDTH; }
  int get_height_internal() override { return HEIGHT; }
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  uint32_t get_buffer_length_() { return uint32_t(WIDTH) * uint32_t(HEIGHT) / 8u; }

  void reset_();
  bool wait_busy_(const char *comment);
  void command_(uint8_t cmd);
  void write_data_(uint8_t value);
  void write_command_data_(uint8_t cmd, const uint8_t *data, size_t len);
  void init_display_();
  void update_full_();

  GPIOPin *dc_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};

  // Tracks the last frame actually pushed to the panel, so update() can skip
  // the (visible, flickery) physical refresh entirely when the rendered
  // content hasn't changed since last time - e-paper refreshes are slow and
  // there's no reason to redo one for identical pixels.
  std::vector<uint8_t> prev_buffer_;
  bool has_prev_buffer_{false};
};

}  // namespace uc8253_epaper
}  // namespace esphome
