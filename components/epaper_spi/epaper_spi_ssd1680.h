#pragma once

#include "epaper_spi.h"

namespace esphome::epaper_spi {

/**
 * SSD1680 monochrome e-paper controller, scoped to the Pimoroni Badger 2350
 * (see models/ssd1680.py for panel-specific notes: native RAM geometry,
 * and citations to Pimoroni's own driver).
 *
 * Buffer: 1 bit per pixel, single plane (bit 1 = white, 0 = black). Every full
 * refresh writes a custom waveform LUT (0x32 + EOPT/GDVC/SDVC/WVCOM), then the
 * same 1bpp buffer to BOTH the "red"/grey-plane RAM (0x26) and the B/W RAM
 * (0x24), then triggers via BTST (0x0C) + Display Update Control 2 (0x22)
 * mode 0xC7 + Activate (0x20). This mirrors Pimoroni's own driver exactly
 * (modules/c/ssd1680/ssd1680.cpp in pimoroni/badger2350) - this panel's OTP
 * memory does not contain a usable full-refresh waveform (confirmed on real
 * hardware: the built-in OTP path, 0x22 mode 0xF7, completes without error
 * but never visibly refreshes the panel). Pimoroni's driver writes the two
 * RAM planes with different bit thresholds of the same greyscale framebuffer
 * to get 4 grey levels out of the 5-level LUT; our buffer is already reduced
 * to 1bpp by the time it reaches this driver, so both planes get identical
 * data, which degrades cleanly to solid black/white rather than true grey.
 *
 * Partial updates load a host LUT (0x32, panel data from the model) and
 * refresh with 0x22=0xCC instead. After each refresh the displayed frame is
 * copied into the OLD RAM bank (0x26) so the next differential partial has a
 * valid reference - this path is unmodified from upstream and unverified for
 * this panel (full_update_every=1 by default, so it's not exercised).
 *
 * BUSY is active-HIGH, which is this component's default (no inversion).
 */
class EPaperSSD1680 : public EPaperBase {
 public:
  EPaperSSD1680(const char *name, uint16_t width, uint16_t height, const uint8_t *init_sequence,
                size_t init_sequence_length, const uint8_t *lut_partial, size_t lut_partial_length)
      : EPaperBase(name, width, height, init_sequence, init_sequence_length, DISPLAY_TYPE_BINARY),
        lut_partial_(lut_partial),
        lut_partial_length_(lut_partial_length) {
    this->buffer_length_ = this->row_width_ * height;  // single 1-bpp plane
  }

 protected:
  bool reset() override;
  bool initialise(bool partial) override;
  bool transfer_data() override;
  void power_on() override {}
  void refresh_screen(bool partial) override;
  void power_off() override;
  void deep_sleep() override;

  void set_ram_area_();
  // Full-refresh custom waveform LUT (0x32) + EOPT/GDVC/SDVC/WVCOM, ported
  // from Pimoroni's write_luts(). Sent once per full refresh (not cached),
  // matching Pimoroni's driver, which never relies on the OTP default.
  void write_waveform_lut_();
  // 0x22 (display update control) with `mode`, then 0x20 (master activation).
  void send_update_(uint8_t mode);
  // Command + data, sent one byte at a time instead of via cmd_data()'s bulk
  // write_array() path. See the class comment for why: live testing on real
  // hardware proved every command byte in our sequence is correct (manually
  // replaying just the trigger bytes via a known-good path caused a genuine
  // ~2s physical refresh), yet ESPHome's driver produced no visible refresh
  // at all with an identical byte sequence sent via write_array() for any
  // multi-byte payload. This narrows the fault to that specific transfer
  // path (suspected Arduino-Pico RP2 SPI library issue with write-only
  // multi-byte transfers, `transfer(ptr, nullptr, length)`) rather than
  // anything about the command sequence itself.
  void write_bytewise_(uint8_t command, const uint8_t *ptr, size_t length);

  // Partial-refresh waveform LUT, supplied by the model (panel-specific).
  const uint8_t *lut_partial_;
  size_t lut_partial_length_;

  // Which RAM plane transfer_data() is currently writing - true for the
  // "red"/grey plane (0x26, written first), false for the B/W plane (0x24).
  // Reset to true once both planes have been written, ready for next refresh.
  bool writing_red_plane_{true};
};

}  // namespace esphome::epaper_spi
