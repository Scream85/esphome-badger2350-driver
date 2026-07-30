#include "epaper_spi_ssd1680.h"

#include <algorithm>

namespace esphome::epaper_spi {

void EPaperSSD1680::write_bytewise_(uint8_t command, const uint8_t *ptr, size_t length) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(command);
  if (length > 0) {
    this->dc_pin_->digital_write(true);
    for (size_t i = 0; i < length; i++)
      this->write_byte(ptr[i]);
  }
  this->disable();
}

bool EPaperSSD1680::initialise(bool partial) {
  // Replay the init sequence (from models/ssd1680.py's get_init_sequence())
  // byte-by-byte via write_bytewise_() instead of EPaperBase's
  // send_init_sequence_(), which uses cmd_data()'s bulk write_array() path
  // for any payload longer than 1 byte. See the header comment on
  // write_bytewise_() for why.
  size_t index = 0;
  while (index != this->init_sequence_length_) {
    const uint8_t cmd = this->init_sequence_[index++];
    const uint8_t x = this->init_sequence_[index++];
    if (x == DELAY_FLAG) {
      delay(cmd);
      continue;
    }
    const uint8_t num_args = x & 0x7F;
    this->write_bytewise_(cmd, this->init_sequence_ + index, num_args);
    index += num_args;
  }
  return true;
}

void EPaperSSD1680::set_ram_area_() {
  // x+ y- (0x01), matching Pimoroni's own driver for this exact panel
  // (modules/c/ssd1680/ssd1680.cpp in pimoroni/badger2350) - the panel's
  // native RAM scan direction counts Y downward, not upward. Using 0x03
  // (x+ y+, the value inherited from ESPHome's GDEY029T94 model) would
  // write the framebuffer vertically flipped relative to native orientation.
  const uint8_t dem[1] = {0x01};
  this->write_bytewise_(0x11, dem, 1);
  const uint8_t srx[2] = {0x00, (uint8_t) ((this->width_ - 1) / 8)};
  this->write_bytewise_(0x44, srx, 2);
  const uint8_t sry[4] = {0x00, 0x00, (uint8_t) ((this->height_ - 1) % 256), (uint8_t) ((this->height_ - 1) / 256)};
  this->write_bytewise_(0x45, sry, 4);
  const uint8_t srxc[1] = {0x00};
  this->write_bytewise_(0x4E, srxc, 1);
  const uint8_t sryc[2] = {0x00, 0x00};
  this->write_bytewise_(0x4F, sryc, 2);
}

// Full-refresh waveform LUT, ported verbatim from Pimoroni's write_luts()
// (modules/c/ssd1680/ssd1680.cpp in pimoroni/badger2350), with the runtime
// `lut_repeat_count` variable (Pimoroni default: 1, "ghost-free but slightly
// slower") hardcoded at its three positions (marked below) rather than made
// configurable - this repo is scoped to one panel, one setting.
static const uint8_t WAVEFORM_LUT[153] = {
    // clang-format off
    // Voltage source levels, groups VS L0-L4 (12 bytes each)
    0x40, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L0
    0xA0, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L1
    0xA8, 0x65, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L2
    0xAA, 0x65, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L3
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VS L4
    // Phase groups: L0, L1, L2, L3, L4, SR?, Repeat (repeat = lut_repeat_count = 1)
    0x02, 0x00, 0x00, 0x05, 0x0A, 0x00, 0x01,  // Group0
    0x19, 0x19, 0x00, 0x02, 0x00, 0x00, 0x01,  // Group1
    0x05, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x01,  // Group2
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Group3 (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Group4 (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Group5 (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Group6 (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Group7 (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Group8 (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Group9 (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Group10 (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Group11 (unused)
    0x44, 0x42, 0x22, 0x22, 0x23, 0x32, 0x00, 0x00, 0x00,  // FR, XON config
    // clang-format on
};

void EPaperSSD1680::write_waveform_lut_() {
  this->write_bytewise_(0x32, WAVEFORM_LUT, sizeof(WAVEFORM_LUT));  // WLR
  const uint8_t eopt[1] = {0x22};
  this->write_bytewise_(0x3F, eopt, 1);  // EOPT
  const uint8_t gdvc[1] = {0x17};
  this->write_bytewise_(0x03, gdvc, 1);  // GDVC
  const uint8_t sdvc[3] = {0x41, 0xAE, 0x32};
  this->write_bytewise_(0x04, sdvc, 3);  // SDVC
  const uint8_t wvcom[1] = {0x28};
  this->write_bytewise_(0x2C, wvcom, 1);  // WVCOM
}

void EPaperSSD1680::send_update_(uint8_t mode) {
  this->write_bytewise_(0x22, &mode, 1);
  this->command(0x20);
}

bool EPaperSSD1680::reset() {
  if (EPaperBase::reset()) {
    // Pimoroni's reset() waits an additional 10ms after releasing the reset
    // line (plus a busy_wait()) before sending anything else - our base FSM's
    // reset_duration_ only covers the LOW pulse, not this post-release
    // settling time. Without it, SWRESET (and everything that follows) may
    // be sent before the chip's power-on-reset recovery is complete.
    delay(10);
    this->command(0x12);  // SWRESET
    return true;
  }
  return false;
}

bool HOT EPaperSSD1680::transfer_data() {
  const uint32_t start_time = millis();
  const size_t frame = this->buffer_length_;

  if (this->current_data_index_ == 0) {
    if (this->writing_red_plane_) {
      // Start of a fresh refresh: write the custom waveform LUT once (never
      // relies on the OTP default - see the class comment in the header),
      // then the "red"/grey-plane RAM (0x26). Same 1bpp data as the B/W
      // plane, since our buffer has no separate grey-plane information.
      this->write_waveform_lut_();
      this->set_ram_area_();
      this->command(0x26);
    } else {
      this->set_ram_area_();
      this->command(0x24);  // write B/W RAM
    }
  }
  this->dc_pin_->digital_write(true);
  this->enable();
  while (this->current_data_index_ < frame) {
    const size_t n = std::min(MAX_TRANSFER_SIZE, frame - this->current_data_index_);
    // split_buffer may be non-contiguous: read element-by-element. Sent one
    // byte at a time (not write_array()'s bulk path) - see write_bytewise_()'s
    // comment in the header for why.
    for (size_t k = 0; k < n; k++)
      this->write_byte(this->buffer_[this->current_data_index_ + k]);
    this->current_data_index_ += n;
    if (millis() - start_time > MAX_TRANSFER_TIME) {
      this->disable();
      return false;
    }
  }
  this->disable();
  this->current_data_index_ = 0;
  if (this->writing_red_plane_) {
    this->writing_red_plane_ = false;
    return false;  // resume next loop() call to write the B/W plane
  }
  this->writing_red_plane_ = true;  // reset for the next refresh
  return true;
}

void EPaperSSD1680::refresh_screen(bool partial) {
  if (partial) {
    this->write_bytewise_(0x32, this->lut_partial_, this->lut_partial_length_);  // host LUT
    this->send_update_(0xCC);
  } else {
    // BTST (booster soft start, no data) + Display Update Control 2 mode
    // 0xC7 (use the LUT just written via write_waveform_lut_(), not OTP) +
    // Activate. Matches Pimoroni's update() exactly.
    this->command(0x0C);  // BTST
    this->send_update_(0xC7);
  }
}

void EPaperSSD1680::power_off() {
  // Copy the just-displayed frame into the OLD RAM bank (0x26) so the next
  // differential partial update has a valid reference. Only needed when partial
  // updates are enabled.
  if (this->is_using_partial_update_()) {
    this->set_ram_area_();
    this->command(0x26);
    this->dc_pin_->digital_write(true);
    this->enable();
    size_t i = 0;
    while (i < this->buffer_length_) {
      const size_t n = std::min(MAX_TRANSFER_SIZE, this->buffer_length_ - i);
      for (size_t k = 0; k < n; k++)
        this->write_byte(this->buffer_[i + k]);
      i += n;
    }
    this->disable();
  }

  this->send_update_(0x83);  // power off
}

void EPaperSSD1680::deep_sleep() {
  const uint8_t data[1] = {0x01};
  this->write_bytewise_(0x10, data, 1);
}

}  // namespace esphome::epaper_spi
