#include "gooddisplay_gdey075t7.h"

#include <cstdint>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace waveshare_epaper {

const char *const GDEY075T7::TAG = "gdey075t7";

int GDEY075T7::get_width_internal() { return WIDTH; }

int GDEY075T7::get_height_internal() { return HEIGHT; }

uint32_t GDEY075T7::idle_timeout_() { return IDLE_TIMEOUT; }

void GDEY075T7::set_full_update_every(uint32_t full_update_every) { this->full_update_every_ = full_update_every; }

void GDEY075T7::full_refresh() {
  this->at_update_ = 0;
  this->update();
}

void GDEY075T7::initialize() {
  RAMAllocator<uint8_t> allocator;
  this->old_data_ = allocator.allocate(this->get_buffer_length_());
  if (this->old_data_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate old data buffer");
    this->mark_failed();
  }
}

void GDEY075T7::display() {
  // Guard against a failed old-data allocation: full_refresh() calls update() ->
  // display() directly, bypassing the scheduler's failed-component skip.
  if (this->old_data_ == nullptr)
    return;

  const bool full = this->at_update_ == 0;
  this->at_update_ = (this->at_update_ + 1) % this->full_update_every_;

  bool ok;
  if (full) {
    // Flashing full-refresh waveform: slowest (~3.5 s) but best de-ghosting.
    this->init_full_();
    ok = this->write_frame_();
  } else {
    // Differential partial waveform: no flicker, fastest (~0.34 s). Accumulates
    // ghosting, which the periodic full refresh clears. The fast path
    // (init_fast_) is retained but currently unused.
    this->init_partial_();
    ok = this->write_partial_();
  }

  // Only clear the warning when the refresh actually succeeded; otherwise the
  // timeout warning set during the refresh must survive.
  if (ok)
    this->status_clear_warning();

  this->deep_sleep();
}

void GDEY075T7::init_full_() {
  reset_();

  this->wait_until_idle_();

  this->command(0x01);  // POWER SETTING
  this->data(0x07);
  this->data(0x07);  // VGH=20V,VGL=-20V
  this->data(0x3f);  // VDH=15V
  this->data(0x3f);  // VDL=-15V

  // Enhanced display drive(Add 0x06 command)
  this->command(0x06);  // Booster Soft Start
  this->data(0x17);
  this->data(0x17);
  this->data(0x28);
  this->data(0x17);

  this->command(0x04);  // POWER ON
  delay(100);
  this->wait_until_idle_();  // waiting for the electronic paper IC to
                             // release the idle signal

  this->command(0x00);  // PANNEL SETTING
  this->data(0x1F);     // KW-3f   KWR-2F BWROTP 0f BWOTP 1f

  this->command(0x61);  // tres
  this->data(0x03);     // source 800
  this->data(0x20);
  this->data(0x01);  // gate 480
  this->data(0xE0);

  this->command(0x15);
  this->data(0x00);

  this->command(0x50);  // VCOM AND DATA INTERVAL SETTING
  this->data(0x10);     // DDX[0]=0: full-refresh data polarity (new bit 1 = black)
  this->data(0x07);

  this->command(0x60);  // TCON SETTING
  this->data(0x22);
}

void GDEY075T7::init_fast_() {
  reset_();

  this->command(0x00);  // PANNEL SETTING
  this->data(0x1F);     // KW-3f   KWR-2F BWROTP 0f BWOTP 1f

  this->command(0x50);  // VCOM AND DATA INTERVAL SETTING
  this->data(0x10);     // DDX[0]=0: same data polarity as the full-refresh path
  this->data(0x07);

  this->command(0x04);  // POWER ON
  delay(100);
  this->wait_until_idle_();  // waiting for the electronic paper IC to
                             // release the idle signal

  // Enhanced display drive(Add 0x06 command)
  this->command(0x06);  // Booster Soft Start
  this->data(0x27);
  this->data(0x27);
  this->data(0x18);
  this->data(0x17);

  this->command(0xE0);  // Cascade setting
  this->data(0x02);
  this->command(0xE5);  // Force temperature -> 0x5A selects the ~1.5 s waveform
  this->data(0x5A);
}

void GDEY075T7::init_partial_() {
  reset_();

  // Re-apply the power/booster settings: reset_() + the per-cycle deep sleep
  // revert them to power-on defaults, and a partial refresh must drive the panel
  // with the same voltages as a full refresh.
  this->command(0x01);  // POWER SETTING
  this->data(0x07);
  this->data(0x07);  // VGH=20V,VGL=-20V
  this->data(0x3f);  // VDH=15V
  this->data(0x3f);  // VDL=-15V

  this->command(0x06);  // Booster Soft Start
  this->data(0x17);
  this->data(0x17);
  this->data(0x28);
  this->data(0x17);

  this->command(0x00);  // PANNEL SETTING
  this->data(0x1F);     // KW-3f   KWR-2F BWROTP 0f BWOTP 1f

  this->command(0x04);  // POWER ON
  delay(100);
  this->wait_until_idle_();  // waiting for the electronic paper IC to
                             // release the idle signal

  this->command(0xE0);  // Cascade setting
  this->data(0x02);
  this->command(0xE5);  // Force temperature -> select fast partial waveform
  this->data(0x6E);
}

bool GDEY075T7::write_frame_() {
  const uint32_t len = this->get_buffer_length_();

  // OLD data = 0x00: fixed reference for the full-refresh LUT.
  this->command(0x10);
  this->start_data_();
  for (uint32_t i = 0; i < len; i++)
    this->write_byte(0x00);
  this->end_data_();

  // NEW data = image. The framebuffer uses bit 1 = white, while the panel
  // expects bit 1 = black under the full-refresh polarity, so invert.
  this->command(0x13);
  this->start_data_();
  for (uint32_t i = 0; i < len; i++)
    this->write_byte(~this->buffer_[i]);
  this->end_data_();

  // Remember the frame just shown as the differential base for partial updates.
  for (uint32_t i = 0; i < len; i++)
    this->old_data_[i] = this->buffer_[i];

  return this->refresh_();
}

bool GDEY075T7::write_partial_() {
  const uint32_t len = this->get_buffer_length_();

  this->command(0x50);  // VCOM AND DATA INTERVAL SETTING
  this->data(0xA9);     // DDX[0]=1: inverts data polarity for partial mode
  this->data(0x07);

  this->command(0x91);  // This command makes the display enter partial mode
  this->command(0x90);  // resolution setting
  this->data(0x00);
  this->data(0x00);  // x-start
  this->data(WIDTH / 256);
  this->data(WIDTH % 256 - 1);  // x-end
  this->data(0x00);             //
  this->data(0x00);             // y-start
  this->data(HEIGHT / 256);
  this->data(HEIGHT % 256 - 1);  // y-end
  this->data(0x01);

  this->command(0x10);  // OLD data = previous frame
  this->start_data_();
  this->write_array(this->old_data_, len);
  this->end_data_();

  // NEW data = current frame. Under 0x50=0xA9 the panel polarity is inverted, so
  // the framebuffer (bit 1 = white) is written as-is.
  this->command(0x13);
  this->start_data_();
  this->write_array(this->buffer_, len);
  this->end_data_();

  for (uint32_t i = 0; i < len; i++)
    this->old_data_[i] = this->buffer_[i];

  const bool ok = this->refresh_();

  this->command(0x92);  // exit partial mode AFTER the refresh

  return ok;
}

bool GDEY075T7::refresh_() {
  this->command(0x12);              // DISPLAY update
  delay(1);                         //!!!The delay here is necessary, 200uS at least!!!
  if (!this->wait_until_idle_()) {  // waiting for the electronic paper IC to
                                    // release the idle signal
    this->status_set_warning();
    return false;
  }
  return true;
}

void GDEY075T7::reset_() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(10);
  }
}

void GDEY075T7::deep_sleep() {
  this->command(0x50);  // VCOM AND DATA INTERVAL SETTING
  this->data(0xf7);     // WBmode:VBDF 17|D7 VBDW 97 VBDB 57    WBRmode:VBDF F7
                        // VBDW 77 VBDB 37  VBDR B7

  this->command(0x02);            // power off
  if (!this->wait_until_idle_())  // waiting for the electronic paper IC to
                                  // release the idle signal
    return;

  this->command(0x07);  // deep sleep
  this->data(0xA5);
}

void GDEY075T7::dump_config() {
  LOG_DISPLAY("", "Good Display e-Paper", this)
  ESP_LOGCONFIG(TAG, "  Model: GDEY075T7");
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace waveshare_epaper
}  // namespace esphome
