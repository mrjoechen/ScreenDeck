#pragma once

#include <Arduino.h>
#include <FS.h>

// Media referenced by a content page lives either in the internal LittleFS
// content partition or on the TF card. Browser uploads prefer ScreenDeck's
// managed TF-card folders and fall back to LittleFS. SD-backed paths carry this
// prefix; everything else is LittleFS.
constexpr char SD_PATH_PREFIX[] = "/sd/";

enum class MediaStoreSdStatus : uint8_t {
  Missing,
  Ready,
  UnsupportedFilesystem,
  UnreadableFilesystem,
};

bool mediaIsSdPath(const String& path);

// Best-effort TF-card mount. The slot shares SCK/MOSI with the ST7701 command
// bus, so this must run after the panel initialization sequence has finished.
// A missing card, a missing slot, or an unreadable filesystem is not an error:
// the device keeps running on internal storage alone.
bool mediaStoreMountSd();
// Retry a missing card at a bounded rate so status views detect hot insertion
// without making every refresh block on three mount attempts.
bool mediaStoreEnsureSdMounted();
void mediaStoreUnmountSd();
// SD-backed VFS calls report hard media errors from whichever task performed
// the I/O. The Arduino loop consumes the event and tears the mount down only
// after LVGL has released its decoder callback and filesystem handles.
void mediaStoreReportSdIoError(int errorCode);
bool mediaStoreTakeSdRemovalDetected();
bool mediaStoreSdMounted();
MediaStoreSdStatus mediaStoreSdStatus();
const char* mediaStoreSdStatusCode();
const char* mediaStoreSdFilesystemName();
const char* mediaStoreSdSupportedFilesystems();
uint32_t mediaStoreSdClockHz();
uint64_t mediaStoreSdTotalBytes();
uint64_t mediaStoreSdUsedBytes();

// Filesystem for a page path, or nullptr when the backing store is not
// available (an SD path while no card is mounted).
fs::FS* mediaFsFor(const String& path);
// Path as the backing filesystem expects it: SD paths lose the /sd prefix.
String mediaNativePath(const String& path);

File mediaOpen(const String& path, const char* mode = FILE_READ);
bool mediaExists(const String& path);
size_t mediaSize(const String& path);
// Commit one complete, browser-processed upload. Managed SD storage is
// preferred when mounted; LittleFS is the fallback. The returned logical path
// is empty on failure. Writes are staged to a .part file before publication.
String mediaStoreCommitUpload(const String& filename, const uint8_t* data,
                              size_t size);
// Deletes LittleFS uploads and files inside ScreenDeck's managed SD folders.
// Arbitrary files elsewhere on the card always remain user-owned.
bool mediaRemove(const String& path);

bool mediaIsSupportedExtension(const String& path);
bool mediaIsAnimatedPath(const String& path);
// GIF header check. LVGL's GIF widget is a player rather than an image
// decoder, so it cannot answer "is this file usable and how large is it?".
bool mediaInspectGif(const String& path, uint16_t& width, uint16_t& height);

// JSON array of playable files found on the card, newest scan first.
String mediaScanSdJson(size_t maxEntries = 120);
