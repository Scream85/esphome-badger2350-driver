# esphome-badger2350-driver

![Badger 2350 front](https://badgewa.re/static/images/badger_web_front.png)

ESPHome external component for the **Pimoroni Badger 2350** (RP2350, 264x176 SSD1680 e-paper badge).

This is a fork of [parkghost/esphome-epaper](https://github.com/parkghost/esphome-epaper), trimmed down
to support this one device only. See [`CLAUDE.md`](./CLAUDE.md) for the full history of why this exists,
what was verified against Pimoroni's own firmware source, and what's still an open gap.

## Hardware specs

From [pimoroni/badger2350](https://github.com/pimoroni/badger2350) (the device's official repo):

* 2.7" 264×176 greyscale e-paper display (driven here as 1-bit black/white — see below)
* RP2350 + 16MB flash + 8MB PSRAM
* WiFi + Bluetooth 5.2
* USB-C + 1,000mAh battery
* User + system buttons
* Four-zone rear lighting

Get one at [shop.pimoroni.com/products/badger-2350](https://shop.pimoroni.com/products/badger-2350).
Official firmware/docs: [pimoroni/badger2350](https://github.com/pimoroni/badger2350) ·
[badgewa.re/docs](https://badgewa.re/docs).

> **Status:** in progress. ESPHome's built-in `waveshare_epaper` component was confirmed *not* to work
> on this panel (wrong controller command set - no image, or an explicit busy-pin timeout). This
> driver's first flash fixed the RAM geometry/data-entry-mode bugs and ran a completely clean,
> error-free refresh cycle at the protocol level — but still no visible image, because this panel's
> OTP memory has no usable full-refresh waveform. The custom waveform LUT from Pimoroni's own driver
> has now been ported (see `CLAUDE.md`) but **not yet flashed/verified on hardware**. Test before
> relying on it.

## Usage

```yaml
esphome:
  name: badger2350w
  friendly_name: Badger RP2350W

rp2:
  board: rpipico2w

external_components:
  - source: github://Scream85/esphome-badger2350-driver
    components: [epaper_spi]

spi:
  clk_pin: GPIO18
  mosi_pin: GPIO19

font:
  - file: "gfonts://Roboto"
    id: font_small
    size: 14

display:
  - platform: epaper_spi
    model: badger2350
    cs_pin: GPIO17
    dc_pin: GPIO20
    busy_pin: GPIO16
    reset_pin: GPIO21
    rotation: 270
    update_interval: 60s
    id: my_badger_display
    lambda: |-
      // Colour is WYSIWYG here (luminance model), unlike ESPHome's classic
      // waveshare_epaper family: draw ink with a DARK colour, not COLOR_ON
      // (which is white and would render blank paper).
      auto ink = Color(0, 0, 0);
      it.fill(Color(255, 255, 255));
      it.printf(8, 2, id(font_small), ink, "Hello Badger!");
```

Notes (see `CLAUDE.md` for details/citations):

- Model dimensions are registered as the panel's **native** RAM geometry (176x264, portrait) — the
  264x176 landscape view comes from `rotation: 270`, not from the model's width/height.
- **Do not** set `busy_pin: { inverted: true }` — busy polarity is built into the driver
  (active-HIGH, matching the Badger 2350's actual wiring).
- Only full refreshes are used by default (`full_update_every: 1`); partial-update support exists in
  the driver but its waveform LUT is a placeholder inherited from an unrelated panel and hasn't been
  verified for the Badger 2350 — don't enable partial updates until that's addressed.

## Acknowledgements

- **[parkghost/esphome-epaper](https://github.com/parkghost/esphome-epaper)**: source of the
  `epaper_spi` FSM architecture and the original `EPaperSSD1680` driver this fork builds on.
- **[pimoroni/badger2350](https://github.com/pimoroni/badger2350)**: reference driver
  (`modules/c/ssd1680/ssd1680.cpp`) used to verify/correct this fork's RAM geometry and data-entry-mode
  byte against real, shipped hardware behaviour.
