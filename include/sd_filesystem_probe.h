#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr size_t SD_FILESYSTEM_SECTOR_SIZE = 512;

enum class SdFilesystemKind : uint8_t {
  Unknown,
  Fat,
  Fat12,
  Fat16,
  Fat32,
  ExFat,
  Ntfs,
};

inline uint32_t sdReadLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

inline uint64_t sdReadLe64(const uint8_t* bytes) {
  return static_cast<uint64_t>(sdReadLe32(bytes)) |
         (static_cast<uint64_t>(sdReadLe32(bytes + 4)) << 32);
}

inline SdFilesystemKind sdFilesystemFromBootSector(const uint8_t* sector) {
  if (!sector) {
    return SdFilesystemKind::Unknown;
  }
  if (std::memcmp(sector + 3, "EXFAT   ", 8) == 0) {
    return SdFilesystemKind::ExFat;
  }
  if (std::memcmp(sector + 3, "NTFS    ", 8) == 0) {
    return SdFilesystemKind::Ntfs;
  }
  if (std::memcmp(sector + 54, "FAT12   ", 8) == 0) {
    return SdFilesystemKind::Fat12;
  }
  if (std::memcmp(sector + 54, "FAT16   ", 8) == 0) {
    return SdFilesystemKind::Fat16;
  }
  if (std::memcmp(sector + 82, "FAT32   ", 8) == 0) {
    return SdFilesystemKind::Fat32;
  }
  return SdFilesystemKind::Unknown;
}

inline bool sdMbrHasSignature(const uint8_t* sector) {
  return sector && sector[510] == 0x55 && sector[511] == 0xAA;
}

inline bool sdMbrIsProtective(const uint8_t* sector) {
  if (!sdMbrHasSignature(sector)) {
    return false;
  }
  for (size_t index = 0; index < 4; ++index) {
    const size_t entry = 446 + index * 16;
    if (sector[entry + 4] == 0xEE) {
      return true;
    }
  }
  return false;
}

inline uint32_t sdMbrFirstPartitionLba(const uint8_t* sector) {
  if (!sdMbrHasSignature(sector)) {
    return 0;
  }
  for (size_t index = 0; index < 4; ++index) {
    const size_t entry = 446 + index * 16;
    const uint8_t partitionType = sector[entry + 4];
    const uint32_t firstLba = sdReadLe32(sector + entry + 8);
    if (partitionType != 0 && firstLba != 0) {
      return firstLba;
    }
  }
  return 0;
}

inline uint64_t sdGptPartitionEntriesLba(const uint8_t* sector) {
  if (!sector || std::memcmp(sector, "EFI PART", 8) != 0) {
    return 0;
  }
  return sdReadLe64(sector + 72);
}

inline uint64_t sdGptFirstPartitionLba(const uint8_t* entriesSector) {
  if (!entriesSector) {
    return 0;
  }
  constexpr size_t GPT_ENTRY_SIZE = 128;
  for (size_t index = 0;
       index < SD_FILESYSTEM_SECTOR_SIZE / GPT_ENTRY_SIZE; ++index) {
    const uint8_t* entry = entriesSector + index * GPT_ENTRY_SIZE;
    bool typeGuidPresent = false;
    for (size_t byte = 0; byte < 16; ++byte) {
      typeGuidPresent = typeGuidPresent || entry[byte] != 0;
    }
    if (!typeGuidPresent) {
      continue;
    }
    const uint64_t firstLba = sdReadLe64(entry + 32);
    if (firstLba != 0) {
      return firstLba;
    }
  }
  return 0;
}

inline const char* sdFilesystemName(SdFilesystemKind filesystem) {
  switch (filesystem) {
    case SdFilesystemKind::Fat:
      return "FAT";
    case SdFilesystemKind::Fat12:
      return "FAT12";
    case SdFilesystemKind::Fat16:
      return "FAT16";
    case SdFilesystemKind::Fat32:
      return "FAT32";
    case SdFilesystemKind::ExFat:
      return "exFAT";
    case SdFilesystemKind::Ntfs:
      return "NTFS";
    case SdFilesystemKind::Unknown:
    default:
      return "unknown";
  }
}
