#include "waveshare_epaper.h"

namespace esphome {
namespace waveshare_epaper {

class GDEY075T7 : public WaveshareEPaper {
 public:
  static const char *const TAG;

  static const uint16_t WIDTH = 800;

  static const uint16_t HEIGHT = 480;

  // Datasheet full update time is 3.5 s typ. @ 25 C and slows down markedly at
  // low temperature, so keep a generous timeout to avoid aborting mid-refresh.
  static const uint16_t IDLE_TIMEOUT = 10000;

  void initialize() override;

  void dump_config() override;

  void display() override;

  void deep_sleep() override;

  void set_full_update_every(uint32_t full_update_every);

  void full_refresh();

 protected:
  int get_width_internal() override;

  int get_height_internal() override;

  uint32_t idle_timeout_() override;

  bool is_busy_pin_inverted_() override { return true; }

  void reset_();

  // Full-refresh initialization: full power/booster setup, drives the panel with
  // the flashing full-refresh waveform for best de-ghosting.
  void init_full_();

  // Fast-refresh initialization (mirrors the vendor EPD_Init_Fast, E5=0x5A):
  // whole-screen ~1.5 s waveform. Cleaner than partial but flashes once.
  // Retained for reference; not used by display() currently.
  void init_fast_();

  // Lightweight partial-refresh initialization (mirrors the vendor
  // EPD_Init_Part), avoids re-running the heavy power/booster setup.
  void init_partial_();

  // Write the whole framebuffer in normal mode (old=0x00, new=~buffer_).
  // Used by the full and (retained) fast paths; the waveform comes from init.
  bool write_frame_();

  // Differential partial refresh (fast, no flicker, accumulates ghosting).
  bool write_partial_();

  // Trigger a display refresh (0x12) and wait for the panel to become idle.
  bool refresh_();

  // Previous frame, used as the differential base for partial updates.
  // Allocated at setup to avoid a large fixed member (48 KB for this panel).
  uint8_t *old_data_{nullptr};

  uint32_t at_update_{0}, full_update_every_{30};
};

}  // namespace waveshare_epaper
}  // namespace esphome
