# ScreenDeck

English | [中文](README_CN.md)

ScreenDeck turns the Guition / Jingcai ESP32-S3-4848S040 into a locally managed, swipeable 480×480 content display.

Flash from a browser with linked device, with no desktop software: **https://mrjoechen.github.io/ScreenDeck/**

<p>
  <img src="site/assets/device_preview_01.JPG" alt="ScreenDeck on a desk showing the on-device settings screen" width="30%">
  <img src="site/assets/device_preview_02.JPG" alt="ScreenDeck playing a full-screen image with date and time overlay" width="30%">
</p>

<p>
  <img src="site/assets/device_preview.jpg" alt="ESP32-S3-4848S040 preview" width="30%">
  <img src="site/assets/device_preview_spec.webp" alt="ESP32-S3-4848S040 specifications" width="30%">
</p>

## Features

- **Local first.** Content and settings stay on the device. There is no ScreenDeck cloud account. The optional LLM narration feature sends an image to the endpoint you configure; the web controller is meant for a trusted local network and has no login.
- **First-boot Wi-Fi setup.** The display starts its own password-protected access point and shows a QR code. Scan it, or open `http://192.168.4.1/`, to save a home network. The AP password is in the QR code and is also `screen1234`.
- **Web controller.** After the display joins the LAN, manage it from the address on screen or `http://screendeck.local/`. Add text pages, upload PNG/JPG/GIF, add files from a TF card, configure image narration, reorder or delete pages, set brightness, and clear the saved Wi-Fi network.
- **Playback.** Horizontal swipes and five-second autoplay cycle through content pages. A manual swipe restarts the timer. Animated GIF pages dwell for 15 seconds. Swipe down to open device settings, then tap **QR** for the controller page; after ten seconds without touch, playback resumes.
- **On-device settings.** The same controls as the web Settings view: 5–100% brightness, fade or horizontal-slide page animation, Chinese/English, manual or network time, the full UTC-offset list (including UTC+05:45), date/time overlays, image-page weather and summaries, minute-precise scheduled sleep, and confirmed Wi-Fi clearing. Changes from either surface stay in sync across restarts.
- **Weather.** Optional weather on image pages. A background task finds the device from its public IP, reads temperature, WMO code, and day/night state from Open-Meteo, and refreshes every 30 minutes.
- **LLM image narration.** Optionally configure an OpenAI-compatible Base URL, API key, and vision-capable model. When image summaries are enabled, the first display of an image without a cached summary generates Chinese narration of at most 64 characters, stores it locally, and reuses it on later displays. The switch is available both on the device and in the web controller; turning it off hides summaries and stops new requests without deleting the cache. Animated GIFs are represented by their currently visible frame.
- **Scheduled sleep.** When a screen-off window begins, the backlight fades to black over 600 ms. A double tap wakes it like a phone: every touch restarts the timeout, and the panel fades out 30 seconds after the last one.
- **Summary updates.** Narration uses bold text, a transparent background, and a leading icon. With summaries enabled, double-tap the summary area (icon or text) on the awake display to regenerate; while queued, generating, or awaiting local save, the summary area shows “Regenerating summary…”. A successfully saved result replaces the old summary, while failure keeps it. One regeneration can wait behind a busy worker, and repeated taps on the same pending image do not stack requests. **Reset all image summaries** in the web LLM settings requires confirmation and clears only summaries, not images or LLM settings. Enabled summaries regenerate on subsequent displays; results from before the reset cannot restore the old cache. If a local save fails, the generated result is retained for storage retries without repeating the LLM call.
- **Storage.** Uploads use a TF card when one is mounted, and fall back to about 12 MB of on-device LittleFS. Files chosen directly from the card stay on the card.

## Hardware

ScreenDeck currently supports one board: **Guition / Jingcai ESP32-S3-4848S040**.

- ESP32-S3 · 16 MB flash · 8 MB PSRAM
- ST7701 480×480 RGB panel · GT911 capacitive touch
- USB for flashing and a TF card slot for media

## Getting started

1. Flash the firmware from **https://mrjoechen.github.io/ScreenDeck/**, or see [Building and flashing](docs/build-and-flash.md) to build from source.
2. Power on the display and scan the Wi-Fi QR code.
3. If the phone does not open the captive portal, visit `http://192.168.4.1/`.
4. Choose the target Wi-Fi and save its password.
5. After restart, scan the controller QR code or open the IP shown on screen from a computer on the same network.

The controller QR page is shown by default only when there are no content pages yet.

## Content

- Upload one or more PNG/JPG files. The browser previews them, center-crops each to 480×480, and converts them to the panel’s RGB565 format before upload.
- Up to 16 content pages. RGB565 image pages fill the screen without stretching or letterboxing.
- GIF files up to 2 MB and 480×480 keep their animation and play centered at native size. Only one GIF plays at a time.
- LLM credentials are stored in the device's local configuration. The API key is never returned by the status endpoint, but the controller itself uses unencrypted HTTP and has no login, so use it only on a trusted LAN.
- Image-summary prompts support up to 2048 Unicode characters, with a live web counter and length validation. Prompts can describe detailed style and focus preferences; generated summaries remain limited to 64 characters, with request format and output structure fixed in firmware.
- MiSans Regular is the device’s Chinese/English typeface: 6,763 GB2312 Chinese characters, English, digits, and common punctuation. Chinese uses 16px; English and clocks use several sizes. Noto Emoji supplies monochrome emoji; other language fonts are excluded. Complex ZWJ, flag, and skin-tone sequences may render as individual symbols; unsupported characters appear as placeholders.

## TF card

- FAT16 and FAT32 are supported. exFAT and NTFS are detected and reported as unsupported. The firmware never formats a card by itself.
- Browse PNG, JPG (`.jpg`, not `.jpeg`), GIF, and `.rgb565` files up to three directories deep, then add a file as a page. Playback reads the file in place; deleting that page never deletes the file.
- Browser uploads go to `/ScreenDeck/media/` (still images), `/ScreenDeck/animations/` (GIFs), and `/ScreenDeck/temp/` (in-progress writes). Removing the last page that uses a managed upload also deletes that upload. Files copied elsewhere on the card remain yours.
- A page that points at the card survives an unplugged card: it asks you to reinsert the card and recovers after **Rescan card**. A card inserted after boot is detected without restarting.

## License

MIT. See [LICENSE](LICENSE).

Third-party components keep their own licenses, including Espressif’s Apache-2.0 LVGL adapter, LVGL, and the [MiSans Font License](docs/MiSans-LICENSE.txt) for the embedded MiSans typeface, plus the [SIL Open Font License](docs/NotoEmoji-OFL.txt) for Noto Emoji.

Build, flashing, the landing page, and GitHub Releases are documented under [docs/](docs/README.md).
