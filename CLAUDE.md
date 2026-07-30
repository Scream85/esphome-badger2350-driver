# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

An **ESPHome external-components** repository (published as `github://Scream85/esphome-badger2350-driver`),
not a standalone app. It is consumed from an ESPHome device YAML via `external_components:`.

This is a **fork of [parkghost/esphome-epaper](https://github.com/parkghost/esphome-epaper)**, trimmed
down to support **exactly one device: the Pimoroni Badger 2350** (RP2350, 264x176 e-paper badge). It
originally carried two component families (`waveshare_epaper` and `epaper_spi`) and drivers for ~10
panels across 8 controller ICs; all of that has been removed. Only `components/epaper_spi/` remains,
containing the FSM core (`epaper_spi.{h,cpp}`) plus a single driver, `EPaperSSD1680`
(`epaper_spi_ssd1680.{h,cpp}`), registered as one model, `Badger2350`, in `models/ssd1680.py`.

Do not add other panels/drivers back into this repo — if you need e-paper support for something else,
fork upstream again instead of expanding this one. Keep this repo single-purpose.

## Why this exists / history

The Badger 2350's e-paper controller is an **SSD1680**, confirmed from Pimoroni's own firmware source
(`modules/c/ssd1680/ssd1680.cpp` in [pimoroni/badger2350](https://github.com/pimoroni/badger2350)).
ESPHome's built-in `waveshare_epaper` component's "2.7in" models (`2.70in`, `2.70inv2`, `2.70in-b`,
`2.70in-bv2`) were tried first and all failed identically (silent no-op or explicit busy-pin timeout) —
they target the older UC8151/IL0373 chip family, a completely different command set (verified by reading
ESPHome's own `waveshare_epaper.cpp` source: `WaveshareEPaper2P7In::initialize()` sends `0x01`/`0xF8`/etc.
UC8151-style commands, nothing resembling SSD1680's `0x01`/`0x11`/`0x44`/`0x45`). This repo's
`EPaperSSD1680` driver (originally written for Good Display's GDEM029T94 panel, also SSD1680) was reused
instead, since it already sends the correct chip-level command language — it only needed a panel-specific
registration for the Badger 2350's exact dimensions.

## Badger 2350 hardware notes (verified against Pimoroni's own source)

- **Native RAM geometry is 176x264 (portrait), not 264x176.** The controller addresses X in bytes
  (176/8 = 22) and Y in rows (264) — confirmed from Pimoroni's `RAM_FLAGS` enum
  (`X_END = 0x15 = (176/8)-1`, `Y_START = 264`). The `Badger2350` model in `models/ssd1680.py` is
  registered as `width=176, height=264` for this reason. The panel is *used* in 264x176 landscape by
  setting `rotation: 270` on the ESPHome `display:` config, not by swapping the model's dimensions.
- **Data entry mode is `0x01` (x+ y-), not `0x03` (x+ y+).** `epaper_spi_ssd1680.cpp`'s
  `set_ram_area_()` hardcodes `0x01` for this reason — the panel's native scan counts Y downward.
  Using `0x03` (inherited from ESPHome's unrelated GDEY029T94 model in upstream `waveshare_epaper`)
  would write the framebuffer vertically flipped.
- **Pin mapping** (verified against Adafruit CircuitPython's `pimoroni_badger2350` board files, which
  match Pimoroni's own schematic): I2C SDA/SCL = GPIO4/5, e-ink BUSY/CS/CLK/MOSI/DC/RESET =
  GPIO16/17/18/19/20/21.
- **Init sequence is trimmed to only what Pimoroni's reference actually sends**: just the
  driver-output-control command (`0x01`). Border waveform (`0x3C`), display-update-control (`0x21`),
  and temperature-sensor-read (`0x18`) were dropped from the inherited GDEY029T94 sequence — Pimoroni's
  `setup()` doesn't send any of them for this panel.
- **OTP default does not work on this panel — confirmed on real hardware.** The first flash used the
  SSD1680's built-in OTP waveform (`DUC2` mode `0xF7`) for full refreshes. Logs showed a completely
  clean, error-free FSM cycle (correct busy-wait timing, no timeout) but the panel never visibly
  refreshed — this panel's OTP memory apparently has no usable full-refresh waveform, so `0xF7`
  completes at the protocol level while doing nothing physically.
- **Ported Pimoroni's custom waveform LUT** (`write_waveform_lut_()`: the 153-byte LUT via `0x32` +
  `EOPT`/`GDVC`/`SDVC`/`WVCOM`, once per full refresh) and changed the trigger to `BTST` (`0x0C`) +
  `0x22` mode `0xC7`. Flashed and logged a completely clean protocol trace, matching Pimoroni's bytes
  exactly — **still no visible refresh.**
- **Root cause found via live hardware comparison, not log reading.** With the factory MicroPython
  firmware confirmed working (ruling out panel/wiring/power), connected directly to it over USB
  (`mpremote`) and used its exposed `ssd1680` module to get ground truth: a real working
  `SSD1680().update()` asserts BUSY **continuously for ~3.66 seconds**. Our driver's busy-waits were
  never longer than ~27ms at any step - nowhere close. Manually replaying **our exact trigger bytes**
  (`0x0C`, `0x22`=`0xC7`, `0x20`) through the factory driver's own `command()` method (bypassing
  ESPHome entirely) produced a genuine ~2s busy period - proving the command bytes themselves are
  100% correct. The fault is specifically in how ESPHome transmits multi-byte payloads: every
  affected command used `cmd_data()`'s bulk `write_array()` path, which for length > 1 goes through
  the Arduino-Pico RP2 SPI library's write-only `transfer(ptr, nullptr, length)`. Single-byte-or-empty
  commands (which use a different code path) were already proven fine by the same live test.
- **Fix: `write_bytewise_()`.** Every command that carries data now sends its payload one byte at a
  time via `write_byte()` (which takes the same known-good single-byte path proven above), instead of
  `cmd_data()`'s bulk transfer. This required overriding `initialise()` too, since the base FSM's
  generic init-sequence replay also goes through `cmd_data()`. **Not yet confirmed on hardware** - this
  is the next thing to verify after a flash.
- **Two RAM planes are written, not one.** Pimoroni writes the framebuffer to both `0x26` ("red"/grey
  plane) and `0x24` (B/W plane) with *different* bit-threshold extractions of the same greyscale pixel
  data, which combined with the 5-level LUT above is how the panel's native 4-grey-level hardware mode
  actually works — this is **not** software dithering, correcting an earlier assumption in this file.
  `transfer_data()` now writes both planes (tracked via `writing_red_plane_`, since the FSM's
  `current_data_index_` alone can't distinguish "byte N of plane A" from "byte N of plane B"). Our
  buffer is already reduced to 1bpp by the time it reaches this driver (no separate grey-plane data
  available), so both planes get identical data - this should degrade cleanly to solid black/white
  rather than reproduce true grey, but hasn't been visually confirmed yet.

## Build / validate / flash

There is no build system in the repo. You exercise the component by pointing a throwaway ESPHome config
at it and driving the `esphome` CLI. Minimal test config:

```yaml
esphome: { name: test }
rp2040: { board: rpipico2w }
external_components:
  - source: { type: local, path: /abs/path/to/components }
    components: [epaper_spi]
spi: { clk_pin: GPIO18, mosi_pin: GPIO19 }
display:
  - platform: epaper_spi
    model: badger2350
    cs_pin: GPIO17
    dc_pin: GPIO20
    reset_pin: GPIO21
    busy_pin: GPIO16
    rotation: 270
    update_interval: never
    lambda: 'it.fill(Color(255,255,255));'
```

- Validate config + schema:  `esphome config test.yaml`
- Compile firmware:          `esphome compile test.yaml`
- OTA flash a live device:   `esphome run <device>.yaml --device <host>.local --no-logs`
- Stream logs:               `esphome logs <device>.yaml --device <ip>`
- **`esphome clean test.yaml` after removing/renaming any component `.cpp`** — PlatformIO caches the source list and errors on a stale `*.cpp.o` target (`Source ... not found`). A clean rebuild fixes it.

### esphome version gotcha

`epaper_spi` needs a recent esphome (dev / ~2026.6+) providing `mipi`, `split_buffer`, `display.add_metadata`. The **CLI** (pipx venv, e.g. 2026.6.4) has these. A bare `python3 -c "import esphome..."` may resolve an **older** `~/.local` install that lacks them — do feature/import checks with the CLI's interpreter (`head -1 $(which esphome)`), not bare `python3`.

## Architecture: epaper_spi (FSM)

`EPaperBase` (`epaper_spi.{h,cpp}`) is a **non-blocking state machine** driven from `loop()`:

```
IDLE → UPDATE → RESET → RESET_END → INITIALISE → TRANSFER_DATA
     → POWER_ON → REFRESH_SCREEN → POWER_OFF → DEEP_SLEEP → IDLE
```

States numerically greater than `SHOULD_WAIT` **auto busy-wait before executing** — so a refresh that takes seconds never blocks `loop()`. `EPaperSSD1680` implements the hooks: `transfer_data()` (chunk RAM writes to ≤`MAX_TRANSFER_SIZE`, yield every `MAX_TRANSFER_TIME` ms, return `false` to resume next loop), `refresh_screen(partial)`, `power_off()`, `deep_sleep()`, `reset()`.

Busy polarity is **built into the driver**, not the YAML: SSD1680 leaves `busy_invert_` false = active-HIGH (this component's default). Do **not** add `busy_pin: {inverted: true}` in YAML.

Mono color mapping follows **upstream's luminance model** (`EPaperBase::color_to_bit`, split at `r+g+b >= 382`), **not** the classic ESPHome `waveshare_epaper` family's `is_on` foreground-key convention. This is WYSIWYG: `COLOR_ON` (white) → white paper, `COLOR_OFF`/black → ink. Draw ink with a **dark** color, not the default `COLOR_ON` (which renders as blank paper).

### Model registry

`display.py` auto-discovers every `models/*.py` (`pkgutil.iter_modules`) — dropping a `.py` in `models/` registers it, no central list. There is exactly one file here, `models/ssd1680.py`, registering exactly one model, `Badger2350`.

- `class_name` (string) → the C++ class (`epaper_spi_ns.class_(...)`), here `"EPaperSSD1680"`.
- `get_init_sequence(config)` returns tuples `(cmd, data...)`, flattened by `mipi.flatten_sequence`.
- `get_constructor_args(config)` supplies extra C++ ctor args after `(name, w, h, seq, len)` — here, the partial-update LUT (currently a placeholder, see hardware notes above).

## Conventions

- Code and comments in **English**; commit messages follow Conventional Commits.
- Provenance matters in comments/docstrings — cite Pimoroni's actual driver file/line when a value comes from there, not just "the datasheet."
- Verify busy polarity + data-bit polarity + RAM geometry against a real reference (Pimoroni's driver, in this repo's case) before trusting an AI-derived sequence — see the hardware notes above for what's been verified this way vs. what's still an open gap (the custom waveform).
