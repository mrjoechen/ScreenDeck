#include "media_store.h"

#include <atomic>
#include <cerrno>

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include "diskio.h"
#include "sd_diskio.h"

#include "raw_image.h"
#include "sd_filesystem_probe.h"

namespace {
// The TF slot shares its clock and data lines with the ST7701 3-wire command
// bus (GPIO 48/47). That is safe here because the panel only receives commands
// during initialization and its chip select (GPIO 39) is left high afterwards,
// so card traffic never reaches the display controller.
constexpr int8_t SD_PIN_SCK = 48;
constexpr int8_t SD_PIN_MOSI = 47;
constexpr int8_t SD_PIN_MISO = 41;
constexpr int8_t SD_PIN_CS = 42;
constexpr char SD_MOUNT_POINT[] = "/sdcard";
constexpr char SD_SUPPORTED_FILESYSTEMS[] = "FAT16/FAT32";
constexpr char SD_MANAGED_ROOT[] = "/ScreenDeck";
constexpr char SD_MANAGED_MEDIA_DIRECTORY[] = "/ScreenDeck/media";
constexpr char SD_MANAGED_ANIMATION_DIRECTORY[] = "/ScreenDeck/animations";
constexpr char SD_MANAGED_TEMP_DIRECTORY[] = "/ScreenDeck/temp";
constexpr char SD_MANAGED_MEDIA_PREFIX[] = "/sd/ScreenDeck/media/";
constexpr char SD_MANAGED_ANIMATION_PREFIX[] =
    "/sd/ScreenDeck/animations/";
constexpr char SD_MANAGED_TEMP_PREFIX[] = "/sd/ScreenDeck/temp/";
constexpr char LITTLEFS_MEDIA_DIRECTORY[] = "/img";
constexpr char LITTLEFS_MEDIA_PREFIX[] = "/img/";
constexpr uint8_t SD_MAX_OPEN_FILES = 5;
constexpr uint32_t SD_MOUNT_RETRY_INTERVAL_MS = 3000;
constexpr uint32_t SD_UNUSABLE_RETRY_INTERVAL_MS = 30000;
// Tried in order. These lines were laid out for the panel's slow command
// traffic, so a card that cannot keep up at 20 MHz still gets a chance.
constexpr uint32_t SD_CLOCK_CANDIDATES[] = {20000000, 10000000, 4000000};
constexpr size_t SD_SCAN_MAX_DEPTH = 3;

SPIClass sdSpi(FSPI);
bool sdMounted = false;
bool sdMountAttempted = false;
uint32_t sdLastMountAttemptAt = 0;
uint32_t sdClockHz = 0;
MediaStoreSdStatus sdStatus = MediaStoreSdStatus::Missing;
SdFilesystemKind sdFilesystem = SdFilesystemKind::Unknown;
std::atomic<bool> sdRemovalDetected{false};

bool hasExtension(const String& path, const char* extension) {
  String lowered(path);
  lowered.toLowerCase();
  return lowered.endsWith(extension);
}

const char* mediaKind(const String& path) {
  if (hasExtension(path, ".gif")) {
    return "animation";
  }
  if (hasExtension(path, RAW_IMAGE_EXTENSION)) {
    return "raw";
  }
  return "image";
}

template <typename ReadSector>
SdFilesystemKind inspectSdFilesystem(ReadSector readSector) {
  uint8_t sector[SD_FILESYSTEM_SECTOR_SIZE] = {};
  if (!readSector(0, sector)) {
    return SdFilesystemKind::Unknown;
  }

  SdFilesystemKind filesystem = sdFilesystemFromBootSector(sector);
  if (filesystem != SdFilesystemKind::Unknown) {
    return filesystem;
  }

  uint64_t volumeStart = 0;
  if (sdMbrIsProtective(sector)) {
    if (!readSector(1, sector)) {
      return SdFilesystemKind::Unknown;
    }
    const uint64_t entriesLba = sdGptPartitionEntriesLba(sector);
    if (entriesLba == 0 || entriesLba > UINT32_MAX ||
        !readSector(static_cast<uint32_t>(entriesLba), sector)) {
      return SdFilesystemKind::Unknown;
    }
    volumeStart = sdGptFirstPartitionLba(sector);
  } else {
    volumeStart = sdMbrFirstPartitionLba(sector);
  }

  if (volumeStart == 0 || volumeStart > UINT32_MAX ||
      !readSector(static_cast<uint32_t>(volumeStart), sector)) {
    return SdFilesystemKind::Unknown;
  }
  return sdFilesystemFromBootSector(sector);
}

SdFilesystemKind inspectMountedSdFilesystem() {
  const SdFilesystemKind filesystem = inspectSdFilesystem(
      [](uint32_t sector, uint8_t* buffer) {
        return SD.readRAW(buffer, sector);
      });
  return filesystem == SdFilesystemKind::Unknown ? SdFilesystemKind::Fat
                                                  : filesystem;
}

struct SdProbeResult {
  bool detected = false;
  SdFilesystemKind filesystem = SdFilesystemKind::Unknown;
};

SdProbeResult probeSdCard(uint32_t clock) {
  SdProbeResult result;
  const uint8_t drive = sdcard_init(SD_PIN_CS, &sdSpi, clock);
  if (drive == 0xFF) {
    return result;
  }

  const DSTATUS diskStatus = disk_initialize(drive);
  const sdcard_type_t cardType = sdcard_type(drive);
  if ((diskStatus & STA_NOINIT) == 0 && cardType != CARD_NONE &&
      cardType != CARD_UNKNOWN) {
    result.detected = true;
    result.filesystem = inspectSdFilesystem(
        [drive](uint32_t sector, uint8_t* buffer) {
          return sd_read_raw(drive, buffer, sector);
        });
  }
  sdcard_uninit(drive);
  return result;
}

bool unsupportedFilesystem(SdFilesystemKind filesystem) {
  return filesystem == SdFilesystemKind::ExFat ||
         filesystem == SdFilesystemKind::Ntfs;
}

bool hardSdIoError(int errorCode) {
  return errorCode == EIO || errorCode == ENODEV;
}

bool managedSdPath(const String& path) {
  return path.indexOf("..") < 0 &&
         (path.startsWith(SD_MANAGED_MEDIA_PREFIX) ||
          path.startsWith(SD_MANAGED_ANIMATION_PREFIX));
}

bool safeUploadFilename(const String& filename) {
  return !filename.isEmpty() && filename.indexOf('/') < 0 &&
         filename.indexOf('\\') < 0 && filename.indexOf("..") < 0 &&
         mediaIsSupportedExtension(filename);
}

bool ensureDirectory(fs::FS* filesystem, const char* path) {
  if (filesystem->exists(path)) {
    return true;
  }
  errno = 0;
  const bool created = filesystem->mkdir(path);
  mediaStoreReportSdIoError(errno);
  return created;
}

bool ensureManagedSdDirectories(bool animation) {
  return ensureDirectory(&SD, SD_MANAGED_ROOT) &&
         ensureDirectory(&SD, SD_MANAGED_TEMP_DIRECTORY) &&
         ensureDirectory(&SD, animation ? SD_MANAGED_ANIMATION_DIRECTORY
                                        : SD_MANAGED_MEDIA_DIRECTORY);
}

String sdUploadPath(const String& filename) {
  return String(mediaIsAnimatedPath(filename)
                    ? SD_MANAGED_ANIMATION_PREFIX
                    : SD_MANAGED_MEDIA_PREFIX) +
         filename;
}

String littleFsUploadPath(const String& filename) {
  return String(LITTLEFS_MEDIA_PREFIX) + filename;
}

bool commitUploadToPath(const String& finalPath, const uint8_t* data,
                        size_t size) {
  fs::FS* filesystem = mediaFsFor(finalPath);
  if (!filesystem || !data || size == 0) {
    return false;
  }

  const bool sdBacked = mediaIsSdPath(finalPath);
  if (sdBacked && !ensureManagedSdDirectories(mediaIsAnimatedPath(finalPath))) {
    return false;
  }
  if (!sdBacked && !ensureDirectory(&LittleFS, LITTLEFS_MEDIA_DIRECTORY)) {
    return false;
  }

  const int slash = finalPath.lastIndexOf('/');
  const String filename =
      slash >= 0 ? finalPath.substring(slash + 1) : finalPath;
  const String tempPath =
      sdBacked ? String(SD_MANAGED_TEMP_PREFIX) + filename + ".part"
               : finalPath + ".part";
  const String nativeTempPath = mediaNativePath(tempPath);
  const String nativeFinalPath = mediaNativePath(finalPath);
  filesystem->remove(nativeTempPath);

  errno = 0;
  File file = filesystem->open(nativeTempPath, FILE_WRITE);
  int errorCode = errno;
  if (!file) {
    if (sdBacked) {
      mediaStoreReportSdIoError(errorCode);
    }
    return false;
  }

  errno = 0;
  const size_t written = file.write(data, size);
  file.flush();
  file.close();
  errorCode = errno;
  if (sdBacked) {
    mediaStoreReportSdIoError(errorCode);
  }
  if (written != size) {
    if (!sdBacked || !hardSdIoError(errorCode)) {
      filesystem->remove(nativeTempPath);
    }
    return false;
  }

  errno = 0;
  File verification = filesystem->open(nativeTempPath, FILE_READ);
  const size_t committedSize = verification ? verification.size() : 0;
  if (verification) {
    verification.close();
  }
  errorCode = errno;
  if (sdBacked) {
    mediaStoreReportSdIoError(errorCode);
  }
  if (committedSize != size) {
    if (!sdBacked || !hardSdIoError(errorCode)) {
      filesystem->remove(nativeTempPath);
    }
    return false;
  }

  errno = 0;
  const bool published =
      filesystem->rename(nativeTempPath, nativeFinalPath);
  errorCode = errno;
  if (sdBacked) {
    mediaStoreReportSdIoError(errorCode);
  }
  if (!published && (!sdBacked || !hardSdIoError(errorCode))) {
    filesystem->remove(nativeTempPath);
  }
  return published;
}

// Stops well before the document is full: a truncated JSON body would reach
// the browser as an unparseable response rather than a short listing.
constexpr size_t SD_SCAN_JSON_CAPACITY = 32768;
constexpr size_t SD_SCAN_JSON_HEADROOM = 4096;

void scanDirectory(File directory, JsonArray& entries, size_t depth,
                   size_t maxEntries) {
  while (entries.size() < maxEntries &&
         entries.memoryUsage() + SD_SCAN_JSON_HEADROOM <
             SD_SCAN_JSON_CAPACITY) {
    errno = 0;
    File entry = directory.openNextFile();
    if (!entry) {
      mediaStoreReportSdIoError(errno);
      break;
    }
    const String name(entry.name());
    const String path(entry.path());
    // Skip the FAT and macOS bookkeeping directories that would otherwise
    // fill the listing with unplayable files.
    if (name.startsWith(".") || name.equalsIgnoreCase("System Volume Information")) {
      entry.close();
      continue;
    }
    if (entry.isDirectory()) {
      if (depth + 1 < SD_SCAN_MAX_DEPTH) {
        scanDirectory(entry, entries, depth + 1, maxEntries);
      }
      entry.close();
      continue;
    }
    if (mediaIsSupportedExtension(path)) {
      JsonObject item = entries.createNestedObject();
      item["path"] = String(SD_PATH_PREFIX) + (path.startsWith("/")
                                                   ? path.substring(1)
                                                   : path);
      item["name"] = name;
      item["size"] = entry.size();
      item["kind"] = mediaKind(path);
    }
    entry.close();
  }
}
}  // namespace

bool mediaIsSdPath(const String& path) {
  return path.startsWith(SD_PATH_PREFIX);
}

bool mediaStoreMountSd() {
  if (sdMounted) {
    return true;
  }
  sdRemovalDetected.store(false);
  sdMountAttempted = true;
  sdLastMountAttemptAt = millis();
  sdClockHz = 0;
  sdStatus = MediaStoreSdStatus::Missing;
  sdFilesystem = SdFilesystemKind::Unknown;
  sdSpi.begin(SD_PIN_SCK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
  const SdProbeResult probe = probeSdCard(
      SD_CLOCK_CANDIDATES[sizeof(SD_CLOCK_CANDIDATES) /
                              sizeof(SD_CLOCK_CANDIDATES[0]) -
                          1]);
  if (!probe.detected) {
    Serial.println("[sd] no TF card detected");
    sdLastMountAttemptAt = millis();
    sdSpi.end();
    return false;
  }

  for (const uint32_t clock : SD_CLOCK_CANDIDATES) {
    if (SD.begin(SD_PIN_CS, sdSpi, clock, SD_MOUNT_POINT, SD_MAX_OPEN_FILES,
                 false)) {
      if (SD.cardType() == CARD_NONE) {
        SD.end();
        continue;
      }
      sdMounted = true;
      sdStatus = MediaStoreSdStatus::Ready;
      sdFilesystem = inspectMountedSdFilesystem();
      sdClockHz = clock;
      Serial.printf("[sd] %s card mounted at %u MHz (%llu MB total)\n",
                    sdFilesystemName(sdFilesystem),
                    static_cast<unsigned>(clock / 1000000),
                    SD.cardSize() / (1024ULL * 1024ULL));
      return true;
    }
    SD.end();
  }
  sdFilesystem = probe.filesystem;
  sdStatus = unsupportedFilesystem(sdFilesystem)
                 ? MediaStoreSdStatus::UnsupportedFilesystem
                 : MediaStoreSdStatus::UnreadableFilesystem;
  Serial.printf("[sd] card detected but %s is not usable; supported: %s\n",
                sdFilesystemName(sdFilesystem), SD_SUPPORTED_FILESYSTEMS);
  sdLastMountAttemptAt = millis();
  sdSpi.end();
  return false;
}

bool mediaStoreEnsureSdMounted() {
  if (sdMounted) {
    return true;
  }
  const uint32_t now = millis();
  const uint32_t retryInterval =
      sdStatus == MediaStoreSdStatus::Missing
          ? SD_MOUNT_RETRY_INTERVAL_MS
          : SD_UNUSABLE_RETRY_INTERVAL_MS;
  if (sdMountAttempted &&
      now - sdLastMountAttemptAt < retryInterval) {
    return false;
  }
  return mediaStoreMountSd();
}

void mediaStoreUnmountSd() {
  if (sdMounted) {
    SD.end();
    sdSpi.end();
  }
  sdMounted = false;
  sdMountAttempted = true;
  sdLastMountAttemptAt = millis();
  sdClockHz = 0;
  sdStatus = MediaStoreSdStatus::Missing;
  sdFilesystem = SdFilesystemKind::Unknown;
  sdRemovalDetected.store(false);
}

void mediaStoreReportSdIoError(int errorCode) {
  if (!sdMounted || (errorCode != EIO && errorCode != ENODEV)) {
    return;
  }
  if (!sdRemovalDetected.exchange(true)) {
    Serial.printf("[sd] card I/O failed (%d); scheduling safe unmount\n",
                  errorCode);
  }
}

bool mediaStoreTakeSdRemovalDetected() {
  return sdRemovalDetected.exchange(false);
}

bool mediaStoreSdMounted() { return sdMounted; }

MediaStoreSdStatus mediaStoreSdStatus() { return sdStatus; }

const char* mediaStoreSdStatusCode() {
  switch (sdStatus) {
    case MediaStoreSdStatus::Ready:
      return "ready";
    case MediaStoreSdStatus::UnsupportedFilesystem:
      return "unsupported";
    case MediaStoreSdStatus::UnreadableFilesystem:
      return "unreadable";
    case MediaStoreSdStatus::Missing:
    default:
      return "missing";
  }
}

const char* mediaStoreSdFilesystemName() {
  return sdFilesystemName(sdFilesystem);
}

const char* mediaStoreSdSupportedFilesystems() {
  return SD_SUPPORTED_FILESYSTEMS;
}

uint32_t mediaStoreSdClockHz() { return sdClockHz; }

uint64_t mediaStoreSdTotalBytes() {
  if (!sdMounted) {
    return 0;
  }
  errno = 0;
  const uint64_t total = SD.totalBytes();
  mediaStoreReportSdIoError(errno);
  return total;
}

uint64_t mediaStoreSdUsedBytes() {
  if (!sdMounted) {
    return 0;
  }
  errno = 0;
  const uint64_t used = SD.usedBytes();
  mediaStoreReportSdIoError(errno);
  return used;
}

fs::FS* mediaFsFor(const String& path) {
  if (!mediaIsSdPath(path)) {
    return &LittleFS;
  }
  return sdMounted ? static_cast<fs::FS*>(&SD) : nullptr;
}

String mediaNativePath(const String& path) {
  if (!mediaIsSdPath(path)) {
    return path;
  }
  // "/sd/photos/a.jpg" -> "/photos/a.jpg"
  return path.substring(sizeof(SD_PATH_PREFIX) - 2);
}

File mediaOpen(const String& path, const char* mode) {
  fs::FS* filesystem = mediaFsFor(path);
  if (!filesystem) {
    return File();
  }
  errno = 0;
  File file = filesystem->open(mediaNativePath(path), mode);
  if (!file && mediaIsSdPath(path)) {
    mediaStoreReportSdIoError(errno);
  }
  return file;
}

bool mediaExists(const String& path) {
  fs::FS* filesystem = mediaFsFor(path);
  if (!filesystem) {
    return false;
  }
  errno = 0;
  const bool exists = filesystem->exists(mediaNativePath(path));
  if (!exists && mediaIsSdPath(path)) {
    mediaStoreReportSdIoError(errno);
  }
  return exists;
}

size_t mediaSize(const String& path) {
  File file = mediaOpen(path);
  if (!file) {
    return 0;
  }
  const size_t size = file.size();
  file.close();
  return size;
}

String mediaStoreCommitUpload(const String& filename, const uint8_t* data,
                              size_t size) {
  if (!safeUploadFilename(filename) || !data || size == 0) {
    return "";
  }

  mediaStoreEnsureSdMounted();
  if (sdMounted) {
    const String sdPath = sdUploadPath(filename);
    if (commitUploadToPath(sdPath, data, size)) {
      Serial.printf("[upload] committed to managed SD storage: %s\n",
                    sdPath.c_str());
      return sdPath;
    }
    Serial.println("[upload] managed SD commit failed; using LittleFS");
  }

  const String internalPath = littleFsUploadPath(filename);
  if (!commitUploadToPath(internalPath, data, size)) {
    return "";
  }
  Serial.printf("[upload] committed to LittleFS: %s\n", internalPath.c_str());
  return internalPath;
}

bool mediaRemove(const String& path) {
  if (path.indexOf("..") >= 0 ||
      (mediaIsSdPath(path) ? !managedSdPath(path)
                           : !path.startsWith(LITTLEFS_MEDIA_PREFIX))) {
    return false;
  }
  fs::FS* filesystem = mediaFsFor(path);
  if (!filesystem) {
    return false;
  }
  errno = 0;
  const bool removed = filesystem->remove(mediaNativePath(path));
  if (mediaIsSdPath(path) && !removed) {
    mediaStoreReportSdIoError(errno);
  }
  return removed;
}

bool mediaIsSupportedExtension(const String& path) {
  // LVGL 8.3's split-JPEG decoder keys on a three character "jpg" tail, so
  // ".jpeg" files are listed as unplayable rather than failing at render time.
  return hasExtension(path, ".png") || hasExtension(path, ".jpg") ||
         hasExtension(path, ".gif") ||
         hasExtension(path, RAW_IMAGE_EXTENSION);
}

bool mediaIsAnimatedPath(const String& path) {
  return hasExtension(path, ".gif");
}

bool mediaInspectGif(const String& path, uint16_t& width, uint16_t& height) {
  if (!mediaIsAnimatedPath(path)) {
    return false;
  }
  File file = mediaOpen(path);
  if (!file) {
    return false;
  }
  // "GIF87a" or "GIF89a", then the logical screen size as two little-endian
  // 16-bit values.
  uint8_t header[10] = {};
  errno = 0;
  const size_t read = file.read(header, sizeof(header));
  if (read != sizeof(header) && mediaIsSdPath(path)) {
    mediaStoreReportSdIoError(errno);
  }
  file.close();
  if (read != sizeof(header) || memcmp(header, "GIF8", 4) != 0 ||
      (header[4] != '7' && header[4] != '9') || header[5] != 'a') {
    return false;
  }
  width = static_cast<uint16_t>(header[6]) |
          (static_cast<uint16_t>(header[7]) << 8);
  height = static_cast<uint16_t>(header[8]) |
           (static_cast<uint16_t>(header[9]) << 8);
  return width > 0 && height > 0;
}

String mediaScanSdJson(size_t maxEntries) {
  DynamicJsonDocument doc(SD_SCAN_JSON_CAPACITY);
  JsonArray entries = doc.to<JsonArray>();
  if (sdMounted) {
    errno = 0;
    File root = SD.open("/");
    if (!root) {
      mediaStoreReportSdIoError(errno);
    }
    if (root && root.isDirectory()) {
      scanDirectory(root, entries, 0, maxEntries);
    }
    if (root) {
      root.close();
    }
  }
  String output;
  serializeJson(entries, output);
  return output;
}
