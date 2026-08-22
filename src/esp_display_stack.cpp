#include "esp_display_stack.h"

#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"

namespace {
constexpr uint16_t SCREEN_WIDTH = 480;
constexpr uint16_t SCREEN_HEIGHT = 480;
// 548 x 518 total pixels per frame with the porches below. 16 MHz yields
// 16000000 / (548 * 518) = 56.4 Hz. The previous 6 MHz value was carried over
// from the Arduino_GFX build, where a low clock was the only way to keep the
// direct-EDMA scanout ahead of PSRAM contention; it left the panel refreshing
// at 21 Hz, which reads as flicker and makes every full-screen page change
// linger for a whole 47 ms frame. The bounce buffer below now absorbs PSRAM
// latency, so the panel can run at its normal rate.
constexpr uint32_t RGB_PIXEL_CLOCK_HZ = 16000000;
constexpr size_t RGB_FRAME_BUFFER_COUNT = 3;
constexpr size_t RGB_BOUNCE_BUFFER_LINES = 20;
constexpr size_t RGB_BOUNCE_BUFFER_PIXELS =
    SCREEN_WIDTH * RGB_BOUNCE_BUFFER_LINES;

constexpr gpio_num_t LCD_SPI_CS = GPIO_NUM_39;
constexpr gpio_num_t LCD_SPI_SCK = GPIO_NUM_48;
constexpr gpio_num_t LCD_SPI_SDA = GPIO_NUM_47;

constexpr gpio_num_t TOUCH_SDA = GPIO_NUM_19;
constexpr gpio_num_t TOUCH_SCL = GPIO_NUM_45;

constexpr char TAG[] = "display_stack";

esp_lcd_panel_handle_t rgbPanel = nullptr;
i2c_master_bus_handle_t touchBus = nullptr;
esp_lcd_panel_io_handle_t touchIo = nullptr;
esp_lcd_touch_handle_t touch = nullptr;
lv_display_t* lvglDisplay = nullptr;
bool adapterStarted = false;

// The panel-specific initialization sequence is carried over byte-for-byte
// from the known-good Guition/Jingcai ESP32-S3-4848S040 configuration. The
// command transport and RGB scanout are now both provided by esp_lcd.
static constexpr uint8_t CMD_FF_10[] = {0x77, 0x01, 0x00, 0x00, 0x10};
static constexpr uint8_t CMD_C0[] = {0x3B, 0x00};
static constexpr uint8_t CMD_C1[] = {0x0D, 0x02};
static constexpr uint8_t CMD_C2[] = {0x31, 0x05};
static constexpr uint8_t CMD_CD[] = {0x00};
static constexpr uint8_t CMD_B0_GAMMA[] = {
    0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
    0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18};
static constexpr uint8_t CMD_B1_GAMMA[] = {
    0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
    0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18};
static constexpr uint8_t CMD_FF_11[] = {0x77, 0x01, 0x00, 0x00, 0x11};
static constexpr uint8_t CMD_B0[] = {0x60};
static constexpr uint8_t CMD_B1[] = {0x32};
static constexpr uint8_t CMD_B2[] = {0x07};
static constexpr uint8_t CMD_B3[] = {0x80};
static constexpr uint8_t CMD_B5[] = {0x49};
static constexpr uint8_t CMD_B7[] = {0x85};
static constexpr uint8_t CMD_B8[] = {0x21};
static constexpr uint8_t CMD_C1_PAGE1[] = {0x78};
static constexpr uint8_t CMD_C2_PAGE1[] = {0x78};
static constexpr uint8_t CMD_E0[] = {0x00, 0x1B, 0x02};
static constexpr uint8_t CMD_E1[] = {
    0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44};
static constexpr uint8_t CMD_E2[] = {
    0x11, 0x11, 0x44, 0x44, 0xED, 0xA0,
    0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00};
static constexpr uint8_t CMD_E3[] = {0x00, 0x00, 0x11, 0x11};
static constexpr uint8_t CMD_E4[] = {0x44, 0x44};
static constexpr uint8_t CMD_E5[] = {
    0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
    0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0};
static constexpr uint8_t CMD_E6[] = {0x00, 0x00, 0x11, 0x11};
static constexpr uint8_t CMD_E7[] = {0x44, 0x44};
static constexpr uint8_t CMD_E8[] = {
    0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
    0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0};
static constexpr uint8_t CMD_EB[] = {0x02, 0x00, 0xE4, 0xE4,
                                      0x88, 0x00, 0x40};
static constexpr uint8_t CMD_EC[] = {0x3C, 0x00};
static constexpr uint8_t CMD_ED[] = {
    0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA};
static constexpr uint8_t CMD_FF_13[] = {0x77, 0x01, 0x00, 0x00, 0x13};
static constexpr uint8_t CMD_E5_PAGE3[] = {0xE4};
static constexpr uint8_t CMD_FF_00[] = {0x77, 0x01, 0x00, 0x00, 0x00};
static constexpr uint8_t CMD_COLMOD[] = {0x60};

static const st7701_lcd_init_cmd_t PANEL_INIT_COMMANDS[] = {
    {0xFF, CMD_FF_10, sizeof(CMD_FF_10), 0},
    {0xC0, CMD_C0, sizeof(CMD_C0), 0},
    {0xC1, CMD_C1, sizeof(CMD_C1), 0},
    {0xC2, CMD_C2, sizeof(CMD_C2), 0},
    {0xCD, CMD_CD, sizeof(CMD_CD), 0},
    {0xB0, CMD_B0_GAMMA, sizeof(CMD_B0_GAMMA), 0},
    {0xB1, CMD_B1_GAMMA, sizeof(CMD_B1_GAMMA), 0},
    {0xFF, CMD_FF_11, sizeof(CMD_FF_11), 0},
    {0xB0, CMD_B0, sizeof(CMD_B0), 0},
    {0xB1, CMD_B1, sizeof(CMD_B1), 0},
    {0xB2, CMD_B2, sizeof(CMD_B2), 0},
    {0xB3, CMD_B3, sizeof(CMD_B3), 0},
    {0xB5, CMD_B5, sizeof(CMD_B5), 0},
    {0xB7, CMD_B7, sizeof(CMD_B7), 0},
    {0xB8, CMD_B8, sizeof(CMD_B8), 0},
    {0xC1, CMD_C1_PAGE1, sizeof(CMD_C1_PAGE1), 0},
    {0xC2, CMD_C2_PAGE1, sizeof(CMD_C2_PAGE1), 0},
    {0xE0, CMD_E0, sizeof(CMD_E0), 0},
    {0xE1, CMD_E1, sizeof(CMD_E1), 0},
    {0xE2, CMD_E2, sizeof(CMD_E2), 0},
    {0xE3, CMD_E3, sizeof(CMD_E3), 0},
    {0xE4, CMD_E4, sizeof(CMD_E4), 0},
    {0xE5, CMD_E5, sizeof(CMD_E5), 0},
    {0xE6, CMD_E6, sizeof(CMD_E6), 0},
    {0xE7, CMD_E7, sizeof(CMD_E7), 0},
    {0xE8, CMD_E8, sizeof(CMD_E8), 0},
    {0xEB, CMD_EB, sizeof(CMD_EB), 0},
    {0xEC, CMD_EC, sizeof(CMD_EC), 0},
    {0xED, CMD_ED, sizeof(CMD_ED), 0},
    {0xFF, CMD_FF_13, sizeof(CMD_FF_13), 0},
    {0xE5, CMD_E5_PAGE3, sizeof(CMD_E5_PAGE3), 0},
    {0xFF, CMD_FF_00, sizeof(CMD_FF_00), 0},
    {0x3A, CMD_COLMOD, sizeof(CMD_COLMOD), 0},
    {0x11, nullptr, 0, 120},
    {0x29, nullptr, 0, 0},
};

bool beginPanel() {
  esp_lcd_panel_io_handle_t lcdCommandIo = nullptr;
  spi_line_config_t lineConfig{};
  lineConfig.cs_io_type = IO_TYPE_GPIO;
  lineConfig.cs_gpio_num = LCD_SPI_CS;
  lineConfig.scl_io_type = IO_TYPE_GPIO;
  lineConfig.scl_gpio_num = LCD_SPI_SCK;
  lineConfig.sda_io_type = IO_TYPE_GPIO;
  lineConfig.sda_gpio_num = LCD_SPI_SDA;
  lineConfig.io_expander = nullptr;

  esp_lcd_panel_io_3wire_spi_config_t ioConfig =
      ST7701_PANEL_IO_3WIRE_SPI_CONFIG(lineConfig, 0);
  esp_err_t result =
      esp_lcd_new_panel_io_3wire_spi(&ioConfig, &lcdCommandIo);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "3-wire command IO failed: %s", esp_err_to_name(result));
    return false;
  }

  esp_lcd_rgb_panel_config_t rgbConfig{};
  rgbConfig.clk_src = LCD_CLK_SRC_DEFAULT;
  rgbConfig.timings.pclk_hz = RGB_PIXEL_CLOCK_HZ;
  rgbConfig.timings.h_res = SCREEN_WIDTH;
  rgbConfig.timings.v_res = SCREEN_HEIGHT;
  rgbConfig.timings.hsync_pulse_width = 8;
  rgbConfig.timings.hsync_back_porch = 50;
  rgbConfig.timings.hsync_front_porch = 10;
  rgbConfig.timings.vsync_pulse_width = 8;
  rgbConfig.timings.vsync_back_porch = 20;
  rgbConfig.timings.vsync_front_porch = 10;
  rgbConfig.timings.flags.hsync_idle_low = false;
  rgbConfig.timings.flags.vsync_idle_low = false;
  rgbConfig.timings.flags.de_idle_high = false;
  rgbConfig.timings.flags.pclk_active_neg = false;
  rgbConfig.timings.flags.pclk_idle_high = false;
  rgbConfig.data_width = 16;
  rgbConfig.bits_per_pixel = 16;
  rgbConfig.num_fbs = RGB_FRAME_BUFFER_COUNT;
  rgbConfig.bounce_buffer_size_px = RGB_BOUNCE_BUFFER_PIXELS;
  rgbConfig.dma_burst_size = 64;
  rgbConfig.hsync_gpio_num = GPIO_NUM_16;
  rgbConfig.vsync_gpio_num = GPIO_NUM_17;
  rgbConfig.de_gpio_num = GPIO_NUM_18;
  rgbConfig.pclk_gpio_num = GPIO_NUM_21;
  rgbConfig.disp_gpio_num = GPIO_NUM_NC;
  const int dataPins[16] = {4,  5, 6,  7,  15, 8, 20, 3,
                            46, 9, 10, 11, 12, 13, 14, 0};
  memcpy(rgbConfig.data_gpio_nums, dataPins, sizeof(dataPins));
  rgbConfig.flags.fb_in_psram = true;
  rgbConfig.flags.double_fb = false;
  rgbConfig.flags.refresh_on_demand = false;
  rgbConfig.flags.no_fb = false;
  rgbConfig.flags.bb_invalidate_cache = false;

  st7701_vendor_config_t vendorConfig{};
  vendorConfig.init_cmds = PANEL_INIT_COMMANDS;
  vendorConfig.init_cmds_size =
      sizeof(PANEL_INIT_COMMANDS) / sizeof(PANEL_INIT_COMMANDS[0]);
  vendorConfig.rgb_config = &rgbConfig;
  vendorConfig.flags.mirror_by_cmd = false;
  // The TF slot reuses these command pins after panel initialization.
  vendorConfig.flags.enable_io_multiplex = true;

  esp_lcd_panel_dev_config_t panelConfig{};
  panelConfig.reset_gpio_num = GPIO_NUM_NC;
  panelConfig.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panelConfig.bits_per_pixel = 16;
  panelConfig.vendor_config = &vendorConfig;

  result =
      esp_lcd_new_panel_st7701(lcdCommandIo, &panelConfig, &rgbPanel);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "ST7701 allocation failed: %s", esp_err_to_name(result));
    return false;
  }

  void* frameBuffers[RGB_FRAME_BUFFER_COUNT] = {};
  result = esp_lcd_rgb_panel_get_frame_buffer(
      rgbPanel, RGB_FRAME_BUFFER_COUNT, &frameBuffers[0], &frameBuffers[1],
      &frameBuffers[2]);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "RGB framebuffer lookup failed: %s",
             esp_err_to_name(result));
    return false;
  }
  for (void* frameBuffer : frameBuffers) {
    memset(frameBuffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
  }

  result = esp_lcd_panel_reset(rgbPanel);
  if (result == ESP_OK) {
    result = esp_lcd_panel_init(rgbPanel);
  }
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "ST7701 initialization failed: %s",
             esp_err_to_name(result));
    return false;
  }

  ESP_LOGI(TAG,
           "RGB ready: 480x480 RGB565, %u MHz (%u Hz), 3 PSRAM framebuffers, "
           "20-line DRAM bounce buffers",
           static_cast<unsigned>(RGB_PIXEL_CLOCK_HZ / 1000000),
           static_cast<unsigned>(
               RGB_PIXEL_CLOCK_HZ /
               ((rgbConfig.timings.hsync_pulse_width +
                 rgbConfig.timings.hsync_back_porch + SCREEN_WIDTH +
                 rgbConfig.timings.hsync_front_porch) *
                (rgbConfig.timings.vsync_pulse_width +
                 rgbConfig.timings.vsync_back_porch + SCREEN_HEIGHT +
                 rgbConfig.timings.vsync_front_porch))));
  ESP_LOGI(TAG, "framebuffers: %p %p %p", frameBuffers[0], frameBuffers[1],
           frameBuffers[2]);
  return true;
}

bool beginTouch() {
  i2c_master_bus_config_t busConfig{};
  busConfig.i2c_port = I2C_NUM_0;
  busConfig.sda_io_num = TOUCH_SDA;
  busConfig.scl_io_num = TOUCH_SCL;
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.intr_priority = 0;
  busConfig.trans_queue_depth = 0;
  busConfig.flags.enable_internal_pullup = true;
  esp_err_t result = i2c_new_master_bus(&busConfig, &touchBus);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "touch I2C bus failed: %s", esp_err_to_name(result));
    return false;
  }

  esp_lcd_touch_config_t touchConfig{};
  touchConfig.x_max = SCREEN_WIDTH;
  touchConfig.y_max = SCREEN_HEIGHT;
  touchConfig.rst_gpio_num = GPIO_NUM_NC;
  touchConfig.int_gpio_num = GPIO_NUM_NC;
  touchConfig.levels.reset = 0;
  touchConfig.levels.interrupt = 0;
  touchConfig.flags.swap_xy = false;
  touchConfig.flags.mirror_x = false;
  touchConfig.flags.mirror_y = false;

  const uint8_t addresses[] = {ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
                               ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP};
  for (uint8_t address : addresses) {
    esp_lcd_panel_io_i2c_config_t ioConfig =
        ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ioConfig.dev_addr = address;
    ioConfig.scl_speed_hz = 400000;
    result = esp_lcd_new_panel_io_i2c(touchBus, &ioConfig, &touchIo);
    if (result != ESP_OK) {
      continue;
    }
    result = esp_lcd_touch_new_i2c_gt911(touchIo, &touchConfig, &touch);
    if (result == ESP_OK) {
      ESP_LOGI(TAG, "GT911 ready at 0x%02X", address);
      return true;
    }
    esp_lcd_panel_io_del(touchIo);
    touchIo = nullptr;
  }

  ESP_LOGW(TAG, "GT911 not detected; touch disabled");
  return false;
}
}  // namespace

bool espDisplayStackBegin() {
  if (!beginPanel()) {
    return false;
  }

  // Touch is optional: a display without a responding controller should still
  // boot and remain manageable from the web interface.
  beginTouch();

  esp_lv_adapter_config_t adapterConfig = ESP_LV_ADAPTER_DEFAULT_CONFIG();
  adapterConfig.task_core_id = 1;
  adapterConfig.task_priority = 6;
  adapterConfig.stack_in_psram = false;
  esp_err_t result = esp_lv_adapter_init(&adapterConfig);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "LVGL adapter initialization failed: %s",
             esp_err_to_name(result));
    return false;
  }

  esp_lv_adapter_display_config_t displayConfig =
      ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
          rgbPanel, nullptr, SCREEN_WIDTH, SCREEN_HEIGHT,
          ESP_LV_ADAPTER_ROTATE_0);
  displayConfig.tear_avoid_mode =
      ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL;
  displayConfig.profile.buffer_height = SCREEN_HEIGHT;
  displayConfig.profile.use_psram = true;
  lvglDisplay = esp_lv_adapter_register_display(&displayConfig);
  if (!lvglDisplay) {
    ESP_LOGE(TAG, "LVGL RGB display registration failed");
    return false;
  }
  ESP_LOGI(TAG, "LVGL registered in TRIPLE_FULL anti-tearing mode");
  return true;
}

bool espDisplayStackStart() {
  if (adapterStarted) {
    return true;
  }
  const esp_err_t result = esp_lv_adapter_start();
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "LVGL worker start failed: %s", esp_err_to_name(result));
    return false;
  }
  adapterStarted = true;
  return true;
}

bool espDisplayStackReadTouch(uint16_t& x, uint16_t& y) {
  if (!touch || esp_lcd_touch_read_data(touch) != ESP_OK) {
    return false;
  }
  esp_lcd_touch_point_data_t point{};
  uint8_t pointCount = 0;
  if (esp_lcd_touch_get_data(touch, &point, &pointCount, 1) != ESP_OK ||
      pointCount == 0) {
    return false;
  }
  x = point.x;
  y = point.y;
  return true;
}

bool espDisplayStackLock(int32_t timeoutMs) {
  return esp_lv_adapter_lock(timeoutMs) == ESP_OK;
}

void espDisplayStackUnlock() {
  esp_lv_adapter_unlock();
}

bool espDisplayStackPause() {
  return !adapterStarted || esp_lv_adapter_pause(-1) == ESP_OK;
}

bool espDisplayStackResume() {
  return !adapterStarted || esp_lv_adapter_resume() == ESP_OK;
}

bool espDisplayStackRefreshNow() {
  return lvglDisplay && esp_lv_adapter_refresh_now(lvglDisplay) == ESP_OK;
}
