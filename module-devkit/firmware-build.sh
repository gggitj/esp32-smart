#!/usr/bin/env bash
set -euo pipefail
# Build the developer firmware against the already-fixed platform service.
# The caller supplies private sdkconfig and package inputs; nothing is flashed.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_PROJECT="${1:-}"
MODE="${2:-}"
BUILD_DIR="${3:-}"
if [[ "$MODE" != BLE && "$MODE" != LAN || -z "$MODULE_PROJECT" || -z "$BUILD_DIR" ]]; then
  echo "Usage: module-devkit/firmware-build.sh MODULE_PROJECT BLE|LAN BUILD_DIR" >&2; exit 2
fi
: "${MODULE_SDK_DIR:=$ROOT/sdk}"
: "${PI_MODULE_SDKCONFIG:?Set PI_MODULE_SDKCONFIG to a private provisioned sdkconfig}"
: "${PI_MODULE_PACKAGE_DIR:?Run module_devkit.py prepare-firmware first}"
export MODULE_SDK_DIR
export PI_MODULE_PACKAGE_DIR
for package_file in embedded_package.json embedded_package.tar.zst; do
  if [[ ! -f "$PI_MODULE_PACKAGE_DIR/$package_file" ]]; then
    echo "Missing $PI_MODULE_PACKAGE_DIR/$package_file" >&2; exit 2
  fi
  cp "$PI_MODULE_PACKAGE_DIR/$package_file" "$MODULE_PROJECT/firmware/$package_file"
done
mkdir -p "$BUILD_DIR"
MODE_SDKCONFIG="$BUILD_DIR/sdkconfig.private"
python3 - "$PI_MODULE_SDKCONFIG" "$MODE_SDKCONFIG" "$MODE" <<'PY'
from pathlib import Path
import sys
source, destination, mode = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
values = {
    "CONFIG_MODULE_TRANSPORT_BLE": "y" if mode == "BLE" else "n",
    "CONFIG_MODULE_TRANSPORT_LAN": "n" if mode == "BLE" else "y",
    "CONFIG_APP_PI_TRANSPORT_BLE": "y" if mode == "BLE" else "n",
    "CONFIG_APP_PI_TRANSPORT_LAN": "n" if mode == "BLE" else "y",
    "CONFIG_APP_PI_PACKAGE_DATA": "y",
    "CONFIG_APP_PI_PACKAGE_HOTSPOT_HTTP": "n",
}
lines = [line for line in source.read_text().splitlines()
         if not any(line.startswith(key + "=") or line == "# " + key + " is not set" for key in values)]
lines.extend(f"{key}={value}" if value == "y" else f"# {key} is not set" for key, value in values.items())
destination.write_text("\n".join(lines) + "\n")
destination.chmod(0o600)
PY
exec idf.py -C "$MODULE_PROJECT/firmware" -B "$BUILD_DIR" \
  -DSDKCONFIG="$MODE_SDKCONFIG" \
  -DPI_MODULE_PACKAGE_DIR="$PI_MODULE_PACKAGE_DIR" build
