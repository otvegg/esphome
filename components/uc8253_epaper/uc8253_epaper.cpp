#include <cstring>

#include "uc8253_epaper.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace uc8253_epaper {

static const char *const TAG = "uc8253_epaper";

// BUSY is active LOW on this panel (LOW = busy, HIGH = idle), per the
// GxEPD2_370_GDEY037T03 constructor which passes `LOW` as the busy-active
// level. If the display hangs waiting for busy, this is the first thing to
// flip.
static const bool BUSY_ACTIVE_LEVEL = false;

// Not from a datasheet number - common vendor/forum guidance for these
// panels is roughly "do a full refresh every 10-50 partial refreshes" to
// clear ghosting that partial waveforms don't fully remove. Lower this if
// ghosting becomes visible, raise it if the periodic full-refresh flash
// becomes the annoyance instead.
static const uint32_t FULL_REFRESH_EVERY_N_UPDATES = 30;

void UC8253EPaper::setup() {
  this->init_internal_(this->get_buffer_length_());
  this->dc_pin_->setup();
  this->dc_pin_->digital_write(false);
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
  }
  if (this->busy_pin_ != nullptr) {
    this->busy_pin_->setup();
    ESP_LOGD(TAG, "Busy pin resting level before any comms: %s",
             this->busy_pin_->digital_read() ? "HIGH" : "LOW");
  }
  this->spi_setup();
  this->reset_();
  ESP_LOGD(TAG, "Busy pin level after reset: %s",
           this->busy_pin_ == nullptr ? "n/a" : (this->busy_pin_->digital_read() ? "HIGH" : "LOW"));
  this->init_display_();
}

void UC8253EPaper::dump_config() {
  ESP_LOGCONFIG(TAG, "UC8253 e-paper (WeAct 3.7in, 240x416)");
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  ESP_LOGCONFIG(TAG, "  Forcing a full refresh every %u partial refreshes", FULL_REFRESH_EVERY_N_UPDATES);
  LOG_UPDATE_INTERVAL(this);
}

void UC8253EPaper::reset_() {
  if (this->reset_pin_ == nullptr)
    return;
  // Mirrors GxEPD2_EPD::_reset() non-pulldown path: preset high, pulse low,
  // back to high, with a settle delay after each transition.
  this->reset_pin_->digital_write(true);
  delay(10);
  this->reset_pin_->digital_write(false);
  delay(10);
  this->reset_pin_->digital_write(true);
  delay(10);
}

bool UC8253EPaper::wait_busy_(const char *comment) {
  if (this->busy_pin_ == nullptr) {
    delay(50);
    return true;
  }
  const bool level_at_entry = this->busy_pin_->digital_read();
  const uint32_t start = millis();
  while (this->busy_pin_->digital_read() == BUSY_ACTIVE_LEVEL) {
    if (millis() - start > 5000) {
      ESP_LOGW(TAG, "Timeout waiting for busy to clear (%s)", comment);
      return false;
    }
    delay(2);
    App.feed_wdt();
  }
  ESP_LOGD(TAG, "wait_busy_(%s): entry=%s, waited %ums", comment, level_at_entry ? "HIGH" : "LOW",
           (unsigned) (millis() - start));
  return true;
}

void UC8253EPaper::command_(uint8_t cmd) {
  ESP_LOGD(TAG, "Command: 0x%02X", cmd);
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  this->disable();
}

void UC8253EPaper::write_data_(uint8_t value) {
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_byte(value);
  this->disable();
}

void UC8253EPaper::write_command_data_(uint8_t cmd, const uint8_t *data, size_t len) {
  ESP_LOGD(TAG, "Command: 0x%02X, %u data byte(s)", cmd, (unsigned) len);
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  if (len > 0) {
    this->dc_pin_->digital_write(true);
    this->write_array(data, len);
  }
  this->disable();
}

// Ported from GxEPD2_370_GDEY037T03::_InitDisplay(). Only the "not
// hibernating" branch is used, since this driver never puts the panel to
// sleep between updates.
void UC8253EPaper::init_display_() {
  this->write_command_data_(0x00, (const uint8_t[]) {0x1E, 0x0D}, 2);  // PANEL SETTING, soft reset
  delay(1);
  this->write_command_data_(0x00, (const uint8_t[]) {0x1F, 0x0D}, 2);  // PANEL SETTING, KW mode
}

// Ported from GxEPD2_370_GDEY037T03::_Update_Full() with useFastFullUpdate
// forced false - the plain, slow full-refresh path. No CCSET/TSSET
// temperature-forcing trick, no re-init afterwards - this is the simplest
// path in the reference driver and the safest starting point on hardware
// this component hasn't been tested against.
void UC8253EPaper::update_full_() {
  this->write_command_data_(0x50, (const uint8_t[]) {0x97}, 1);  // VCOM AND DATA INTERVAL SETTING
  this->command_(0x04);                                          // POWER ON
  this->wait_busy_("power on");
  this->command_(0x12);  // DISPLAY REFRESH
  this->wait_busy_("refresh");
  this->command_(0x02);  // POWER OFF
  this->wait_busy_("power off");
}

void UC8253EPaper::update() {
  const uint32_t update_start = millis();
  this->do_update_();

  const uint32_t len = this->get_buffer_length_();
  if (this->has_prev_buffer_ && this->prev_buffer_.size() == len &&
      memcmp(this->prev_buffer_.data(), this->buffer_, len) == 0) {
    ESP_LOGD(TAG, "Rendered content unchanged, skipping physical refresh");
    return;
  }
  // has_prev_buffer_ being false here means this is the very first real
  // refresh since boot - the panel's internal "previous" RAM is undefined at
  // that point, so partial refresh's diff-against-previous wouldn't be
  // meaningful yet. Capture that before we set it true below.
  const bool is_first_refresh = !this->has_prev_buffer_;
  this->prev_buffer_.assign(this->buffer_, this->buffer_ + len);
  this->has_prev_buffer_ = true;

  // Write the whole framebuffer to "current" (0x13) memory. Mirrors
  // GxEPD2_370_GDEY037T03::_writeScreenBuffer(0x13, ...): a single
  // continuous SPI burst, no partial-window addressing needed since we
  // always redraw the full screen (partial-window addressing is only
  // needed for writing an arbitrary sub-rectangle, which this driver never
  // does).
  ESP_LOGD(TAG, "Sending framebuffer (%u bytes)", (unsigned) len);
  this->send_buffer_to_(0x13);

  const bool force_full = is_first_refresh || this->updates_since_full_refresh_ >= FULL_REFRESH_EVERY_N_UPDATES;
  if (force_full) {
    this->update_full_();
    this->updates_since_full_refresh_ = 0;
  } else {
    this->update_part_();
    this->updates_since_full_refresh_++;
  }

  // Sync the panel's "previous" memory (0x10) to match what's now actually
  // on screen. Ported from GxEPD2's writeImageAgain() pattern: partial
  // refresh works by having the panel diff current vs. previous RAM, so
  // previous must track reality or the next partial refresh's diff (and
  // thus which pixels it bothers to touch) will be wrong.
  this->send_buffer_to_(0x10);

  ESP_LOGD(TAG, "update() took %ums total (%s)", (unsigned) (millis() - update_start),
           force_full ? "full" : "partial");
}

void UC8253EPaper::send_buffer_to_(uint8_t target_command) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(target_command);
  this->dc_pin_->digital_write(true);
  this->write_array(this->buffer_, this->get_buffer_length_());
  this->disable();
}

// Ported from GxEPD2_370_GDEY037T03::_setPartialRamArea(0, 0, WIDTH, HEIGHT),
// hardcoded for the full 240x416 panel since this driver always redraws the
// whole screen. Recompute these bytes if WIDTH/HEIGHT ever change:
//   x_end   = (WIDTH - 1) | 0x07        = 239 | 0x07 = 0xEF
//   y_end   = HEIGHT - 1                = 415         = 0x019F
void UC8253EPaper::send_partial_window_() {
  this->write_command_data_(0x90, (const uint8_t[]) {0x00, 0xEF, 0x00, 0x00, 0x01, 0x9F, 0x01}, 7);
}

// Ported from GxEPD2_370_GDEY037T03::_Update_Part(). Same shape as
// update_full_(), but wrapped in the partial-window commands (0x91/0x92) and
// using the partial-waveform VCOM setting (0xD7 vs. full refresh's 0x97) -
// this is what makes the panel apply a faster/gentler LUT that only visibly
// touches the pixels it thinks changed, instead of flashing the whole panel.
void UC8253EPaper::update_part_() {
  this->command_(0x91);  // PARTIAL IN
  this->send_partial_window_();

  this->write_command_data_(0x50, (const uint8_t[]) {0xD7}, 1);  // VCOM AND DATA INTERVAL SETTING, partial waveform
  this->command_(0x04);                                          // POWER ON
  this->wait_busy_("power on (partial)");
  this->command_(0x12);  // DISPLAY REFRESH
  this->wait_busy_("refresh (partial)");
  this->command_(0x02);  // POWER OFF
  this->wait_busy_("power off (partial)");

  this->command_(0x92);  // PARTIAL OUT
}

void HOT UC8253EPaper::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
    return;
  const uint32_t pos = (uint32_t(x) + uint32_t(y) * WIDTH) / 8u;
  const uint8_t subpos = x & 0x07;
  // 1 = white, 0 = black (matches the panel's native buffer convention -
  // clearScreen() in the reference driver fills with 0xFF for white).
  if (color.is_on()) {
    this->buffer_[pos] &= ~(0x80 >> subpos);
  } else {
    this->buffer_[pos] |= 0x80 >> subpos;
  }
}

}  // namespace uc8253_epaper
}  // namespace esphome
