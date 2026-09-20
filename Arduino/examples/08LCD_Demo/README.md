# HXESP4848 LCD Test

This sketch initializes the complete display path, not only the backlight:

- TCA9554 on I2C0 (`SCL=GPIO40`, `SDA=GPIO41`)
- ST7701S reset through TCA9554 P0
- ST7701S vendor initialization through 3-wire SPI (`SDA=GPIO1`, `SCL=GPIO2`)
- 480 x 480 RGB565 pixel bus at 16 MHz
- RGB green data `G1=GPIO47`, `G2=GPIO48`
- Full-screen RGB565 white (`0xFFFF`) before enabling the backlight boost through TCA9554 P3
- GPIO38 held low for the maximum software-controlled backlight brightness

Board settings:

- Board: ESP32S3 Dev Module
- Flash size: 16 MB
- PSRAM: OPI PSRAM

Expected result: a stable, uniformly white full-screen image at maximum software-controlled backlight brightness.
