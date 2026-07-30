#include "epaper_spi_ssd1680.h"

#include <algorithm>

namespace esphome::epaper_spi {

void EPaperSSD1680::set_ram_area_() {
  // x+ y- (0x01), matching Pimoroni's own driver for this exact panel
  // (modules/c/ssd1680/ssd1680.cpp in pimoroni/badger2350) - the panel's
  // native RAM scan direction counts Y downward, not upward. Using 0x03
  // (x+ y+, the value inherited from ESPHome's GDEY029T94 model) would
  // write the framebuffer vertically flipped relative to native orientation.
  this->cmd_data(0x11, {0x01});
  this->cmd_data(0x44, {0x00, (uint8_t) ((this->width_ - 1) / 8)});
  this->cmd_data(0x45, {0x00, 0x00, (uint8_t) ((this->height_ - 1) % 256), (uint8_t) ((this->height_ - 1) / 256)});
  this->cmd_data(0x4E, {0x00});
  this->cmd_data(0x4F, {0x00, 0x00});
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
  this->cmd_data(0x32, WAVEFORM_LUT, sizeof(WAVEFORM_LUT));  // WLR
  this->cmd_data(0x3F, {0x22});                              // EOPT
  this->cmd_data(0x03, {0x17});                              // GDVC
  this->cmd_data(0x04, {0x41, 0xAE, 0x32});                  // SDVC
  this->cmd_data(0x2C, {0x28});                              // WVCOM
}

void EPaperSSD1680::send_update_(uint8_t mode) {
  this->cmd_data(0x22, {mode});
  this->command(0x20);
}

bool EPaperSSD1680::reset() {
  if (EPaperBase::reset()) {
    this->command(0x12);  // SWRESET
    return true;
  }
  return false;
}

bool HOT EPaperSSD1680::transfer_data() {
  const uint32_t start_time = millis();
  const size_t frame = this->buffer_length_;
  uint8_t bytes_to_send[MAX_TRANSFER_SIZE];

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
  this->start_data_();
  while (this->current_data_index_ < frame) {
    const size_t n = std::min(MAX_TRANSFER_SIZE, frame - this->current_data_index_);
    // split_buffer may be non-contiguous: read element-by-element.
    for (size_t k = 0; k < n; k++)
      bytes_to_send[k] = this->buffer_[this->current_data_index_ + k];
    this->write_array(bytes_to_send, n);
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
    this->cmd_data(0x32, this->lut_partial_, this->lut_partial_length_);  // host LUT
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
    this->start_data_();
    uint8_t bytes_to_send[MAX_TRANSFER_SIZE];
    size_t i = 0;
    while (i < this->buffer_length_) {
      const size_t n = std::min(MAX_TRANSFER_SIZE, this->buffer_length_ - i);
      // split_buffer may be non-contiguous: read element-by-element.
      for (size_t k = 0; k < n; k++)
        bytes_to_send[k] = this->buffer_[i + k];
      this->write_array(bytes_to_send, n);
      i += n;
    }
    this->disable();
  }

  this->send_update_(0x83);  // power off
}

void EPaperSSD1680::deep_sleep() {
  this->cmd_data(0x10, {0x01});  // enter deep sleep
}

}  // namespace esphome::epaper_spi
