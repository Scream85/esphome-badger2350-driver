"""UC8179 e-paper controller.

Panels:
- GDEY075T7 (Good Display):     800x480 monochrome
- P750057-MF1-A (Good Display): 800x480 black/white/red

UC8179 BUSY is active-LOW; the driver handles that (busy_invert_), so no
`inverted:` is needed on the busy pin in YAML.
"""

from . import RequiredPinsModel


class UC8179Mono(RequiredPinsModel):
    def __init__(self, name, **defaults):
        super().__init__(name, "EPaperUC8179", **defaults)

    def get_init_sequence(self, config):
        width, height = self.get_dimensions(config)
        return (
            (0x00, 0x1F),  # PANEL SETTING: KW (black/white) mode
            (0x61, (width >> 8) & 0xFF, width & 0xFF, (height >> 8) & 0xFF, height & 0xFF),  # TRES (resolution)
            (0x15, 0x00),  # DUAL SPI (disabled)
            (0x50, 0x10, 0x07),  # VCOM AND DATA INTERVAL SETTING
            (0x60, 0x22),  # TCON SETTING
        )


class UC8179BWR(RequiredPinsModel):
    def __init__(self, name, **defaults):
        super().__init__(name, "EPaperUC8179BWR", **defaults)

    def get_init_sequence(self, config):
        width, height = self.get_dimensions(config)
        return (
            (0x00, 0x0F),  # PANEL SETTING: KWR (black/white/red) mode
            (0x61, (width >> 8) & 0xFF, width & 0xFF, (height >> 8) & 0xFF, height & 0xFF),  # TRES (resolution)
            (0x15, 0x00),  # DUAL SPI (disabled)
            (0x50, 0x11, 0x07),  # VCOM AND DATA INTERVAL SETTING
            (0x60, 0x22),  # TCON SETTING
        )


UC8179Mono("GDEY075T7", width=800, height=480, data_rate="4MHz", minimum_update_interval="10s")
UC8179BWR("P750057-MF1-A", width=800, height=480, data_rate="4MHz", minimum_update_interval="30s")
