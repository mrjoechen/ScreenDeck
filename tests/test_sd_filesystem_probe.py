#!/usr/bin/env python3
"""Host-side tests for TF-card boot-sector classification."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SdFilesystemProbeTests(unittest.TestCase):
    def test_boot_sectors_and_partition_tables_are_classified(self) -> None:
        source = textwrap.dedent(
            r'''
            #include <cassert>
            #include <cstdint>
            #include <cstring>

            #include "sd_filesystem_probe.h"

            void putLe32(uint8_t* target, uint32_t value) {
              target[0] = value & 0xFF;
              target[1] = (value >> 8) & 0xFF;
              target[2] = (value >> 16) & 0xFF;
              target[3] = (value >> 24) & 0xFF;
            }

            void putLe64(uint8_t* target, uint64_t value) {
              putLe32(target, static_cast<uint32_t>(value));
              putLe32(target + 4, static_cast<uint32_t>(value >> 32));
            }

            int main() {
              uint8_t sector[SD_FILESYSTEM_SECTOR_SIZE] = {};

              std::memcpy(sector + 3, "EXFAT   ", 8);
              assert(sdFilesystemFromBootSector(sector) == SdFilesystemKind::ExFat);
              assert(std::strcmp(sdFilesystemName(SdFilesystemKind::ExFat), "exFAT") == 0);

              std::memset(sector, 0, sizeof(sector));
              std::memcpy(sector + 3, "NTFS    ", 8);
              assert(sdFilesystemFromBootSector(sector) == SdFilesystemKind::Ntfs);

              std::memset(sector, 0, sizeof(sector));
              std::memcpy(sector + 54, "FAT16   ", 8);
              assert(sdFilesystemFromBootSector(sector) == SdFilesystemKind::Fat16);

              std::memset(sector, 0, sizeof(sector));
              std::memcpy(sector + 82, "FAT32   ", 8);
              assert(sdFilesystemFromBootSector(sector) == SdFilesystemKind::Fat32);

              std::memset(sector, 0, sizeof(sector));
              sector[510] = 0x55;
              sector[511] = 0xAA;
              sector[446 + 4] = 0x07;
              putLe32(sector + 446 + 8, 2048);
              assert(sdMbrFirstPartitionLba(sector) == 2048);
              assert(!sdMbrIsProtective(sector));

              sector[446 + 4] = 0xEE;
              assert(sdMbrIsProtective(sector));

              std::memset(sector, 0, sizeof(sector));
              std::memcpy(sector, "EFI PART", 8);
              putLe64(sector + 72, 2);
              assert(sdGptPartitionEntriesLba(sector) == 2);

              std::memset(sector, 0, sizeof(sector));
              sector[0] = 1;
              putLe64(sector + 32, 4096);
              assert(sdGptFirstPartitionLba(sector) == 4096);

              std::memset(sector, 0, sizeof(sector));
              assert(sdFilesystemFromBootSector(sector) == SdFilesystemKind::Unknown);
              return 0;
            }
            '''
        )
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "sd-filesystem-probe"
            compiled = subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "include"),
                    "-x",
                    "c++",
                    "-",
                    "-o",
                    str(executable),
                ],
                input=source,
                text=True,
                capture_output=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
