# ESP32-S3 RGB UI 技术栈建议

日期：2026-08-01

## 结论

当前残留、闪烁和上传期间花屏的根因不在 LVGL 控件本身，而在 480×480 RGB 屏的持续扫描、PSRAM/Flash 带宽竞争、帧缓冲所有权和 VSYNC 交接。仅更换控件库不能解决。

本项目建议迁移到：

1. ESP-IDF 5.5.x
2. Espressif `esp_lcd` + `esp_lcd_st7701`
3. Espressif `esp_lvgl_adapter`
4. LVGL（第一阶段保留 8.x，降低迁移风险；稳定后再评估 9.x）

播放页可进一步采用混合架构：全屏 RGB565 图片由 `esp_lcd` 三缓冲直接播放，二维码、设置页和时间日期叠层继续由 LVGL 管理。这样能让图片切换成为原子帧缓冲交换，而不是控件重绘。

## 为什么不继续修补现有方案

- Arduino_GFX 的官方 README 标注，`ESP32RGBPanel` 只支持 arduino-esp32 2.x，不再支持 3.0；当前项目是 Arduino-ESP32 3.3.7。这是明显的版本/架构错配。
- Espressif 官方 FAQ 说明，RGB LCD 的画面漂移或花屏可能来自 PSRAM 与 Flash 共用带宽、大块 Flash/PSRAM 访问、PCLK 过高等因素。
- FAQ 明确把 VSYNC 中重启 RGB 驱动列为“不推荐”：不能彻底避免问题，并会降低帧率。因此手动 `esp_lcd_rgb_panel_restart()` 不应继续作为主修复方案。
- 上传图片时 LittleFS 写入、Wi-Fi、图片缓存和 RGB EDMA 同时争用外部存储带宽，恰好会放大这些问题。

## 官方能力

`esp_lcd` 的 RGB 驱动支持最多三个帧缓冲。官方文档将双 PSRAM 帧缓冲描述为最简单的抗撕裂方式：CPU 和 EDMA 分别操作不同缓冲区。驱动还支持内部 SRAM bounce buffer；对于 Wi-Fi 或连续 Flash 写入场景，FAQ 建议结合 PSRAM XIP 与 bounce buffer 配置，但需要按芯片、分辨率和 PCLK 实测。

`esp_lvgl_adapter` 支持 LVGL 8 和 9，专门统一显示注册、抗撕裂和线程安全。它给 RGB 屏提供 `DOUBLE_FULL`、`TRIPLE_FULL`、`DOUBLE_DIRECT` 和 `TRIPLE_PARTIAL` 模式。官方选择表把 `TRIPLE_FULL` 定位为全屏/大面积更新，正适合本项目的整页图片切换；它要求三块 LCD 帧缓冲。

Espressif 自己的 ESP32-S3 LCD BSP 给出了两类抗撕裂组合：双缓冲 + LVGL direct mode，以及双/三缓冲 + full refresh；三缓冲 full refresh 可提升帧率，但会占用更多 PSRAM 和带宽。

## 方案比较

| 方案 | 对根因的覆盖 | 迁移成本 | 建议 |
| --- | --- | --- | --- |
| ESP-IDF + esp_lcd + esp_lvgl_adapter + LVGL | 完整：官方 RGB DMA、三缓冲、线程安全和专用抗撕裂模式 | 中高 | 首选 |
| ESP32_Display_Panel + LVGL（Arduino） | 乐鑫维护，支持 ST7701、Arduino 和本机 Jingcai 4848S040 板型 | 中 | 低风险过渡方案 |
| LovyanGFX + LVGL/自绘 | 支持 ESP32-S3 RGB、DMA、Sprite 和 GT911；帧同步仍需自行设计 | 中 | 若必须保留 Arduino，可作为备选 |
| 继续 Arduino_GFX | 当前 RGB 后端与 Arduino-ESP32 3.x 明显错配 | 低但风险持续 | 不建议 |
| SquareLine Studio | 只是 LVGL UI 设计/代码生成工具，不处理 RGB 扫描和 DMA | 低 | 不能解决本问题 |
| TFT_eSPI / Adafruit_GFX / U8g2 | 不针对本机 480×480 RGB 连续扫描的帧同步问题 | 不合适 | 不建议 |

## 推荐架构

```text
Web / Wi-Fi / LittleFS / 配置逻辑（保留）
                 |
       页面模型与图片缓存（保留）
                 |
      +----------+-----------+
      |                      |
全屏图片播放器            QR / 设置 / 时间叠层
esp_lcd 三缓冲             LVGL 8 + esp_lvgl_adapter
      |                      |
      +----------+-----------+
                 |
       esp_lcd RGB + VSYNC/EDMA
                 |
          ST7701 480×480
```

第一阶段可以全部保留在 LVGL 中，通过 `esp_lvgl_adapter` 的 `TRIPLE_FULL` 路径验证稳定性；如果上传和自动轮播仍有带宽压力，再利用 adapter 的 Dummy Draw/直接显示能力把图片播放路径拆出。480×480 RGB565 三帧约占 1.32 MiB，当前 8 MB PSRAM 足够容纳。

## 迁移顺序

1. 不动网页、配置、LittleFS、页面模型和手势行为，只建立新的显示后端。
2. 将 ST7701 RGB 时序和 GT911 触摸迁到 Espressif 官方 `esp_lcd` / `esp_lcd_touch` 接口。
3. 保留 LVGL 8 页面代码，改由 `esp_lvgl_adapter` 管理刷新完成、锁和缓冲区，避免在同一次迁移中再引入 LVGL 9 API 改造。
4. 本项目优先验证 `TRIPLE_FULL`；再比较 `DOUBLE_FULL`，以无残留、无闪烁、上传稳定为准，而不是单看 FPS。
5. 针对 Wi-Fi + LittleFS 写入启用并验证官方建议的 PSRAM XIP、bounce buffer、64-byte cache line、任务核绑定及合理 PCLK。
6. 做长时间组合测试：自动轮播、连续滑动、图片上传、二维码下拉、息屏双击唤醒和时间叠层同时运行。

## 主要官方来源

- Espressif RGB LCD 驱动（多帧缓冲和 bounce buffer）：https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- Espressif ST7701 RGB 驱动：https://components.espressif.com/components/espressif/esp_lcd_st7701/versions/2.0.2/readme
- Espressif LCD FAQ（RGB 花屏原因、PSRAM XIP/bounce buffer、VSYNC restart 不推荐）：https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html
- Espressif `esp_lvgl_adapter`（抗撕裂模式、PSRAM 配置和 Dummy Draw）：https://components.espressif.com/components/espressif/esp_lvgl_adapter/versions/0.6.3/readme
- Espressif ESP32-S3 LCD BSP 抗撕裂模式：https://components.espressif.com/components/espressif/esp32_s3_lcd_ev_board/versions/2.1.0
- Espressif `ESP32_Display_Panel`：https://github.com/esp-arduino-libs/ESP32_Display_Panel
- Arduino_GFX README（ESP32RGBPanel 版本限制）：https://github.com/moononournation/Arduino_GFX/blob/master/README.md
- LovyanGFX README：https://github.com/lovyan03/LovyanGFX
- LVGL 8 显示接口（full refresh/direct mode）：https://docs.lvgl.io/8.2/porting/display.html
