#!/usr/bin/env bash
set -euo pipefail
SOURCE_DIR="$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")"
source "$SOURCE_DIR/scripts/env.sh"
exec python3 "$SOURCE_DIR/tools/desktop_demo.py" --build "${BUILD_DIR:-$SOURCE_DIR/build}" "$@"
