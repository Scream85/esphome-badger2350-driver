"""SSD1680 monochrome e-paper controller.

Panel:
- Pimoroni Badger 2350: native RAM geometry is 176x264 (portrait) - the
  controller addresses X in bytes (176/8 = 22) and Y in rows (264), per
  Pimoroni's own driver (modules/c/ssd1680/ssd1680.cpp,
  github.com/pimoroni/badger2350). The panel is *used* in 264x176 landscape;
  that's produced by ESPHome's `rotation: 270` on the display, not by
  registering 264x176 here. Vendor markets it as 4-level greyscale, achieved
  by Pimoroni's own MicroPython firmware via software dithering on top of a
  monochrome panel - not a native hardware mode, and not implemented here.
  This repo drives it as plain 1-bit black/white.

Full refresh uses the controller's built-in OTP waveform (DUC2 mode 0xF7).
Pimoroni's own driver instead writes a custom-tuned waveform/LUT before every
refresh (write_luts(): WLR/EOPT/GDVC/SDVC/WVCOM) and never uses the OTP
default. This repo has NOT ported that custom waveform - if the OTP default
produces a visibly wrong/ghosted image (as opposed to no image / a protocol
failure), that waveform is the next thing to port, using write_luts() as the
reference. BUSY is active-HIGH (this component's default, matching
Pimoroni's plain `gpio_get(BUSY)` with no inversion).

This repo is scoped to the Badger 2350 only - see CLAUDE.md.
"""

import esphome.codegen as cg
from esphome.core import ID

from ..display import CONF_INIT_SEQUENCE_ID
from . import RequiredPinsModel


class SSD1680(RequiredPinsModel):
    def __init__(self, name, lut_partial, **defaults):
        super().__init__(name, "EPaperSSD1680", **defaults)
        self.lut_partial = lut_partial

    def get_constructor_args(self, config) -> tuple:
        base = config[CONF_INIT_SEQUENCE_ID].id
        lut_partial = cg.static_const_array(
            ID(base + "_lut_partial", type=cg.uint8), self.lut_partial
        )
        return (lut_partial, len(self.lut_partial))

    def get_init_sequence(self, config):
        _, height = self.get_dimensions(config)
        # SWRESET (0x12) is issued in reset(); the FSM waits for idle before this.
        # Only the driver-output-control command is sent here, matching
        # Pimoroni's setup() exactly. (border waveform / display-update-control /
        # temperature-sensor commands from ESPHome's unrelated GDEY029T94 model
        # were dropped - Pimoroni's reference doesn't send them for this panel.)
        return (
            (
                0x01,
                (height - 1) & 0xFF,
                (height - 1) >> 8,
                0x00,
            ),  # driver output control
        )


# Placeholder partial-refresh waveform LUT, carried over from Good Display's
# GDEM029T94 (128x296) panel (based on GxEPD2 GxEPD2_290_T94_V2), 153 bytes.
# NOT verified against the Badger 2350's actual panel. It is only ever sent to
# the controller if partial updates are enabled (full_update_every=1, the
# default here, means every refresh is a full refresh and this LUT is unused).
# Do not enable partial updates for this model until this LUT has been
# verified on real hardware - see CLAUDE.md's caution on AI-derived sequences.
# fmt: off
_PLACEHOLDER_LUT_PART = [
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
]
# fmt: on

SSD1680(
    "Badger2350",
    lut_partial=_PLACEHOLDER_LUT_PART,
    width=176,   # native RAM geometry - see module docstring. Use
    height=264,  # `rotation: 270` on the display to get 264x176 landscape.
    data_rate="4MHz",
    minimum_update_interval="1s",
)
