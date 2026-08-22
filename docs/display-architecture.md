# Display architecture

The display path uses the ESP-IDF 5.5 stack end to end:

- `esp_lcd_panel_io_additions` sends the ST7701 initialization sequence over
  its 3-wire command interface.
- `esp_lcd_st7701` owns the 480×480 RGB panel and RGB DMA.
- `esp_lcd_touch_gt911` owns the I2C touch controller.
- `esp_lvgl_adapter` owns LVGL's tick, render worker, VSYNC callbacks, and
  framebuffer lifecycle in `TRIPLE_FULL` anti-tearing mode.
- Three full-screen RGB565 framebuffers live in OPI PSRAM. A 20-line internal
  SRAM bounce buffer isolates continuous RGB scanout from PSRAM stalls.

Arduino remains as an ESP-IDF compatibility component for Wi-Fi, the web
portal, LittleFS, and preference APIs. It does not own display, touch, LVGL
scheduling, or frame synchronization.

All LittleFS and NVS writes pause the LVGL worker. The RGB bounce-buffer
interrupt is IRAM-safe (`CONFIG_LCD_RGB_ISR_IRAM_SAFE`), so scanout survives
the cache being disabled during a flash write. Only long writes still gate the
backlight. On-screen settings taps are coalesced into one commit 1.2 s after
the last change; brightness previews stay in RAM while dragging and enter that
save path only when the slider is released.

The firmware keeps the current and adjacent still images in a three-slot PSRAM
cache. Existing PNG pages remain compatible and are decoded into this cache
once, rather than on every swipe. RGB565 image pages use about 450 KB each. A
GIF holds roughly four bytes of PSRAM per pixel while it is on screen.

Espressif's adapter publishes only completed frames at the RGB driver's frame
boundary, avoiding partial redraws, stale-frame replay, and backlight blanking
during page switches.

## TF card bus sharing

The card slot shares its clock and data lines with the ST7701 command bus
(SCK GPIO 48, MOSI GPIO 47) and adds MISO GPIO 41 and CS GPIO 42. The card is
mounted only after the panel initialization sequence has finished; the display
controller's chip select stays high afterwards, so card traffic never reaches
it.

The ST7701 command IO releases GPIO 48/47 after panel init, then the card is
probed at 20, 10, and 4 MHz. A missing card, missing slot, or unreadable
filesystem is not an error.

Backlight PWM is on GPIO 38 at 150 Hz. This driver cannot follow a fast
carrier: at 20 kHz every duty below full scale collapsed the LED string.

GT911 capacitive touch is on GPIO 19/45.
