#include "raw_image.h"

#include "media_store.h"

namespace {
constexpr uint8_t RAW_IMAGE_MAGIC[] = {'S', 'D', 'R', '5'};

bool readHeader(File& file, uint16_t& width, uint16_t& height) {
  uint8_t header[RAW_IMAGE_HEADER_BYTES] = {};
  if (file.read(header, sizeof(header)) != sizeof(header) ||
      memcmp(header, RAW_IMAGE_MAGIC, sizeof(RAW_IMAGE_MAGIC)) != 0) {
    return false;
  }
  width = static_cast<uint16_t>(header[4]) |
          (static_cast<uint16_t>(header[5]) << 8);
  height = static_cast<uint16_t>(header[6]) |
           (static_cast<uint16_t>(header[7]) << 8);
  return width == RAW_IMAGE_WIDTH && height == RAW_IMAGE_HEIGHT;
}
}  // namespace

bool rawImageInspect(const String& path, uint16_t& width, uint16_t& height) {
  if (!path.endsWith(RAW_IMAGE_EXTENSION)) {
    return false;
  }
  File file = mediaOpen(path);
  if (!file || file.size() != RAW_IMAGE_FILE_BYTES) {
    if (file) {
      file.close();
    }
    return false;
  }
  const bool valid = readHeader(file, width, height);
  file.close();
  return valid;
}

bool rawImageLoadPixels(const String& path, uint8_t* destination,
                        size_t capacity) {
  if (!destination || capacity < RAW_IMAGE_PIXEL_BYTES) {
    return false;
  }
  File file = mediaOpen(path);
  if (!file || file.size() != RAW_IMAGE_FILE_BYTES) {
    if (file) {
      file.close();
    }
    return false;
  }
  uint16_t width = 0;
  uint16_t height = 0;
  if (!readHeader(file, width, height)) {
    file.close();
    return false;
  }

  size_t totalRead = 0;
  while (totalRead < RAW_IMAGE_PIXEL_BYTES) {
    const size_t bytesRead =
        file.read(destination + totalRead, RAW_IMAGE_PIXEL_BYTES - totalRead);
    if (bytesRead == 0) {
      file.close();
      return false;
    }
    totalRead += bytesRead;
  }
  file.close();
  return true;
}
