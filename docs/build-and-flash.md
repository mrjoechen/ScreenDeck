# Building and flashing

ScreenDeck is a PlatformIO ESP-IDF/Arduino hybrid build pinned to ESP-IDF
5.5.2 and Arduino-ESP32 3.3.7. Display, touch, and LVGL versions are locked in
`src/idf_component.yml`. `components/esp_lvgl_adapter` is a pinned 0.6.3 tree
with a PPA hang workaround.

## Build

Install [PlatformIO Core](https://platformio.org/install/cli), then from the
repository root:

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run
```

The compiled application is `.pio/build/esp32s3/firmware.bin`.

Local builds stamp `SCREENDECK_VERSION` and `SCREENDECK_BUILD_TIME` via
`tools/inject_version.py`. Untagged trees report a development version. Serial
output and `GET /api/status` expose `firmwareVersion` and `firmwareBuildTime`.
The on-device **System** settings screen shows the same values.

## USB flash

Upload is fixed at 115200 because some USB-to-serial links corrupt at higher
baud rates.

```sh
pio run --target upload
```

PlatformIO writes the bootloader, partition table, and application. The first
boot formats the LittleFS content partition if needed.

## Factory image

For a blank device, flash a merged factory image at address `0x000000`. GitHub
Pages serves the current image through ESP Web Tools; locally:

```sh
pio run
./tools/merge-web-firmware.sh
```

The merged file is written to
`site/firmware/esp32-s3-4848s040/screendeck-esp32s3-4848s040-factory.bin`
and is gitignored.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/`, `include/` | ScreenDeck firmware |
| `components/esp_lvgl_adapter/` | Pinned LVGL adapter with local patch |
| `site/` | Landing page and web installer |
| `tests/` | Release guards (no hardware required) |
| `tools/` | Embed-file helper, version stamp, factory-image merge |
| `.github/workflows/release.yml` | Tag-only firmware build, GitHub Release, and Pages deploy |
