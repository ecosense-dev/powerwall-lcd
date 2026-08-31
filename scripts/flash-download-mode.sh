#!/usr/bin/env bash
# Flash via the UART1 Type-C (CH343). Auto-download uses DTR/RTS; do not use
# --before no_reset: opening the CDC port on macOS resets the chip out of
# download mode and esptool then sees "No serial data received".

set -euo pipefail
PORT="${1:-/dev/cu.usbmodem5C941570181}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

export PATH="/opt/homebrew/bin:/usr/bin:/bin:${PATH}"
export IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
# shellcheck disable=SC1091
. "$IDF_PATH/export.sh"

cd "$ROOT/build"
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  @flash_args

echo "Flash OK. Se lo schermo resta nero, premi RESET."
