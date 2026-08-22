#!/usr/bin/env bash
# Merge bootloader + partition table + application into the GitHub Pages factory image.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/.pio/build/esp32s3"
OUT_DIR="${ROOT}/site/firmware/esp32-s3-4848s040"
OUT="${OUT_DIR}/screendeck-esp32s3-4848s040-factory.bin"

ESPTOOL_PYTHON="${ESPTOOL_PYTHON:-}"
if [[ -z "${ESPTOOL_PYTHON}" ]]; then
  if [[ -x "${HOME}/.platformio/penv/bin/python" ]]; then
    ESPTOOL_PYTHON="${HOME}/.platformio/penv/bin/python"
  else
    ESPTOOL_PYTHON="python3"
  fi
fi

for part in bootloader.bin partitions.bin firmware.bin; do
  if [[ ! -s "${BUILD}/${part}" ]]; then
    echo "missing ${BUILD}/${part}; run: pio run" >&2
    exit 1
  fi
done

mkdir -p "${OUT_DIR}"
"${ESPTOOL_PYTHON}" -m esptool \
  --chip esp32s3 merge-bin \
  -o "${OUT}" \
  --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x0 "${BUILD}/bootloader.bin" \
  0x8000 "${BUILD}/partitions.bin" \
  0x10000 "${BUILD}/firmware.bin"

"${ESPTOOL_PYTHON}" -m esptool --chip esp32s3 image-info "${OUT}"
echo "wrote ${OUT}"
