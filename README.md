# ESPHome E-Paper Display Integration

This repo provides ESPHome external components for various e-paper displays:

- **`epaper_spi`** (default) — a port of ESPHome-dev's newer non-blocking
  `epaper_spi` architecture, organized by controller IC (UC81xx / SSD16xx).
- **`waveshare_epaper`** (classic) — the original blocking driver style,
  kept for existing configs (see the collapsed section below).

## `epaper_spi`

Requires a recent ESPHome (dev / ~2026.6+, which provides `mipi` /
`split_buffer` / `display.add_metadata`).

### Supported models

10 Good Display / DKE panels across 8 controller ICs. Each panel is a model
registration (`models/*.py`) on top of a per-controller C++ driver.

`✅` = verified on real hardware this way. `compile` = builds and the command
sequence is a faithful port of the vendor/GxEPD2 reference, but not yet run on
the physical panel (BWR plane polarity in particular should be confirmed on
hardware).

| Model | Controller IC | Colour | Res. | Non-full refresh | C++ driver class | Hardware | Vendor / reference |
|---|---|---|---|---|---|---|---|
| `e0213a09` | SSD1675A (IL3897) | mono | 104×212 | differential partial | `EPaperSSD1675` | ✅ | [GxEPD2 · GxEPD2_213_B72](https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_213_B72.cpp) |
| `gdeh029a1` | SSD1608 (IL3820) | mono | 128×296 | differential partial | `EPaperSSD1608` | ✅ | [GxEPD2 · GxEPD2_290](https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_290.cpp) |
| `gdem029t94` | SSD1680 | mono | 128×296 | differential partial | `EPaperSSD1680` | ✅ | [GxEPD2 · GxEPD2_290_T94_V2](https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_290_T94_V2.cpp) |
| `gdew029t5d` | UC8151D | mono | 128×296 | differential partial | `EPaperUC8151` | ✅ | [Good Display · product/210](https://www.good-display.com/product/210.html) |
| `gdey029z95` | SSD1680 | B/W/R | 128×296 | fast | `EPaperSSD1680BWR` | ✅ | [Good Display · product/527](https://www.good-display.com/product/527.html) |
| `gdew042m01` | UC8176 (IL0398) | mono | 400×300 | differential partial | `EPaperUC8176` | compile | [GxEPD2 · GxEPD2_420_M01](https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_420_M01.cpp) |
| `depg0420` | SSD1683 | B/W/R | 400×300 | fast | `EPaperSSD1683` | ✅ | [GxEPD2 · GxEPD2_420c_GDEY042Z98](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdey3c/GxEPD2_420c_GDEY042Z98.cpp) |
| `gdew042z15` | UC8176 (IL0398) | B/W/R | 400×300 | full only | `EPaperUC8176BWR` | ✅ | [E-Paper Display · productId=322](https://www.e-paper-display.com/products_detail/productId=322.html) |
| `gdey075t7` | UC8179 | mono | 800×480 | differential partial | `EPaperUC8179` | ✅ | Good Display A32-GDEY075T7 Arduino demo + UC8179 / GDEY075T7 datasheets |
| `p750057-mf1-a` | UC8179 | B/W/R | 800×480 | fast | `EPaperUC8179BWR` | ✅ | [gooddisplayshare/ESP32epdx · GDEY075Z08](https://github.com/gooddisplayshare/ESP32epdx/tree/main/examples/3-Colors%20(BWR)/7.5/GDEY075Z08) |

### Usage

```yaml
external_components:
  - source: github://parkghost/esphome-epaper
    components: [epaper_spi]

font:
  - file: "gfonts://Roboto"
    id: roboto_36
    size: 36

spi:
  clk_pin: GPIO4
  mosi_pin: GPIO6

display:
  - platform: epaper_spi
    cs_pin: GPIO0
    dc_pin: GPIO1
    busy_pin: GPIO3
    reset_pin: GPIO2
    model: gdey075t7
    rotation: 270
    update_interval: 24h
    full_update_every: 30
    id: my_display
    lambda: |-
      // Colours map by luminance (WYSIWYG): draw ink with a DARK colour.
      // The default draw colour COLOR_ON is white and would render blank.
      auto BLACK = Color(0, 0, 0);
      it.printf(it.get_width()/2, it.get_height()/2, id(roboto_36), BLACK,
                TextAlign::CENTER, "Hello World!");
```

Drawing conventions (differ from `waveshare_epaper`):

- **Colour is WYSIWYG** (luminance model, unlike `waveshare_epaper`'s
  `COLOR_ON` = ink): draw ink with `Color(0, 0, 0)`, **not** the default
  `COLOR_ON`, which is white and renders as blank paper.
- **Busy polarity is built into the driver** — do **not** set
  `busy_pin: { inverted: true }` in YAML.

### Controller ICs & datasheets

| IC | Vendor | Panels | Busy | Notes |
|---|---|---|---|---|
| UC8179 | UltraChip | gdey075t7, p750057-mf1-a | active-LOW | `0x10`/`0x13` RAM, `0x50` DDX polarity, `0xE5` fast waveform |
| UC8176 (IL0398) | UltraChip | gdew042m01, gdew042z15 | active-LOW | UC81xx; register LUTs `0x20`-`0x24`, partial window `0x90`/`0x91`/`0x92` |
| UC8151D | UltraChip | gdew029t5d | active-LOW | UC81xx; register LUTs `0x20`-`0x24` |
| SSD1608 (IL3820) | Solomon Systech | gdeh029a1 | active-HIGH | host LUT via `0x32` |
| SSD1675A (IL3897) | Solomon Systech | e0213a09 | active-HIGH | host LUT via `0x32`; copies frame to OLD bank `0x26` |
| SSD1680 | Solomon Systech | gdem029t94, gdey029z95 | active-HIGH | full = OTP (`0x22=0xF7`), partial = host LUT (`0x22=0xCC`) |
| SSD1683 | Solomon Systech | depg0420 | active-HIGH | BWR planes `0x24`/`0x26`, fast = `0x1A`+`0x91`/`0xC7` |

Datasheets referenced during this port (UltraChip / Solomon Systech, obtained
from the panel vendor): `UC8179`, `UC8176`, `UC8151D`, `SSD1608`, `SSD1675A`,
`SSD1680`, `SSD1683`, plus the Good Display panel specs (e.g. `GDEY075T7`,
`GDEM029T94`) and A32 Arduino demo bundles (`A32-GDEY075T7`, `A32-GDEM029T94`).

## `waveshare_epaper` (classic driver)

<details>
<summary>Supported displays and usage for the classic <code>waveshare_epaper</code> component</summary>

### Supported Displays

The following e-paper display models are supported by this component:

| Model         | Size  | Colors | Resolution | Partial Refresh | Fast Refresh | Tested                                        | Useful for                 | Driver IC         |
|---------------|-------|--------|------------|-----------------|--------------|-----------------------------------------------|----------------------------|-------------------|
| e0213a09      | 2.13" | B/W    | 212x104    | Y               | N            | E213A09N(HINK-E0213A07-A1)                    |                            | SSD1675A(IL3897)  |
| gdeh029a1     | 2.9"  | B/W    | 296x128    | Y               | N            | E029A01(E029A01-FPCA-V2.0) / (E029A01-FPC-A1) | Good Display GDEH029A1     | SSD1608(IL3820)   |
| gdem029t94    | 2.9"  | B/W    | 296x128    | Y               | N            | Waveshare 2.9" SKU-12563 (FPC-7519rev.b)      | Good Display GDEM029T94    | SSD1680           |
| gdew029t5d    | 2.9"  | B/W    | 296x128    | Y               | N            | WF0290T5(WFT0290CZ10 LW) / (WFT0290CZ10 LP)   | Good Display GDEW029T5D    | UC8151D           |
| gdey029z95    | 2.9"  | B/W/R  | 296x128    | N               | Y            | (FPC-A005 20.06.15 TRX)                       | Good Display GDEY029Z95    | SSD1680           |
| gdew042m01    | 4.2"  | B/W    | 400x300    | Y               | N            | WF0420T80CZ35230H(WF0420CZ35 LW)              | Good Display GDEW042M01    | UC8176(IL0398)    |
| depg0420      | 4.2"  | B/W/R  | 400x300    | N               | Y            | DEPG0420(FPC-190)                             | Good Display GDEY042Z98    | SSD1683           |
| gdew042z15    | 4.2"  | B/W/R  | 400x300    | N               | N            | WF0420T80CZ15(WFT0420CZ15 LW)                 | Good Display GDEW042Z15    | UC8176(IL0398)    |
| gdey075t7     | 7.5"  | B/W    | 800x480    | Y               | Y            | GDEY075T7(FPC-C001 21.08.30 HB)               | Good Display GDEY075T7     | UC8179            |
| p750057-mf1-a | 7.5"  | B/W/R  | 800x480    | N               | Y            | (P750057-MF1-A)                               | Good Display GDEY075Z08    | UC8179            |

### Usage

```yaml
external_components:
  - source: github://parkghost/esphome-epaper
    components: [waveshare_epaper]

font:
  - file: "gfonts://Roboto"
    id: roboto_36
    size: 36

spi:
  clk_pin: GPIO4
  mosi_pin: GPIO6

display:
  - platform: waveshare_epaper
    cs_pin: GPIO0
    dc_pin:  GPIO1
    busy_pin: GPIO3
    reset_pin: GPIO2
    model: e0213a09
    rotation: 270
    update_interval: 24h
    full_update_every: 30
    id: my_display
    lambda: |-
      int width = it.get_width();
      int height = it.get_height();
      it.printf(width/2, height/2, id(roboto_36), TextAlign::CENTER, "Hello World!");
```

</details>

## Examples

For examples and configurations, visit the [ESPHome E-Paper Examples](https://github.com/parkghost/esphome-epaper-examples).

## Acknowledgements

- **[ESPHome Waveshare E-Paper Display](https://esphome.io/components/display/waveshare_epaper.html)**: The ESPHome waveshare_epaper component provides the base for integrating e-paper displays with ESPHome, enabling easy configuration and control of supported displays.

- **[GxEPD2](https://github.com/ZinggJM/GxEPD2)**: This library has been instrumental in driving e-paper displays. Its open-source implementation allows for smooth handling of various e-paper models.
