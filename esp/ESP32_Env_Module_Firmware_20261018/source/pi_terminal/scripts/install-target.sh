#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")"
: "${PRODUCT_CONFIG:?Set PRODUCT_CONFIG to the production product.json}"
: "${PUBLISHER_KEY:?Set PUBLISHER_KEY to the production publisher.pem}"
: "${CONTROL_DEVICE:?Set CONTROL_DEVICE to /dev/serial/by-id/...}"

[[ -f "$PRODUCT_CONFIG" ]] || { echo "product config not found: $PRODUCT_CONFIG" >&2; exit 1; }
[[ -f "$PUBLISHER_KEY" ]] || { echo "publisher key not found: $PUBLISHER_KEY" >&2; exit 1; }
[[ -x "$SOURCE_DIR/build-aarch64/orange_pi_terminal" ]] || { echo "run scripts/build-cross.sh first" >&2; exit 1; }

install -D -m 0755 "$SOURCE_DIR/build-aarch64/orange_pi_terminal" /usr/bin/orange_pi_terminal
install -D -m 0755 "$SOURCE_DIR/tools/package_tool.py" /usr/libexec/orange-pi-terminal/package_tool.py
install -D -m 0644 "$PRODUCT_CONFIG" /etc/orange-pi-terminal/product.json
install -D -m 0644 "$PUBLISHER_KEY" /etc/orange-pi-terminal/publisher.pem
install -D -m 0644 "$SOURCE_DIR/config/environment.example" /etc/orange-pi-terminal/environment
sed -i "s#^ORANGE_PI_CONTROL_DEVICE=.*#ORANGE_PI_CONTROL_DEVICE=$CONTROL_DEVICE#" /etc/orange-pi-terminal/environment
install -D -m 0644 "$SOURCE_DIR/systemd/orange-pi-terminal.service" /usr/lib/systemd/user/orange-pi-terminal.service
install -d -m 0750 "${XDG_STATE_HOME:-$HOME/.local/state}/orange-pi-terminal"
install -d -m 0750 "${XDG_DATA_HOME:-$HOME/.local/share}/orange-pi-terminal/modules"
echo "Installed. Validate the graphical user session, serial permissions, BlueZ and NetworkManager before enabling the user service."
