#!/usr/bin/env bash
set -euo pipefail
SOURCE_DIR="$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")"
source "$SOURCE_DIR/scripts/env.sh"
BUILD_DIR="${BUILD_DIR:-$SOURCE_DIR/build}"
bash "$SOURCE_DIR/scripts/build.sh"
ctest --test-dir "$BUILD_DIR" --output-on-failure --output-junit "$BUILD_DIR/test-results.xml"
