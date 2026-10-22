#!/usr/bin/env bash
set -euo pipefail
# Compatibility entry: use the actual Qt6 application, not the old Qt5 mockup.
ROOT="$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")"
bash "$ROOT/scripts/build.sh"
exec bash "$ROOT/scripts/run-desktop.sh" "$@"
