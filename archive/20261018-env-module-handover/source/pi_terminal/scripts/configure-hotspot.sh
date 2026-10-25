#!/usr/bin/env bash
set -euo pipefail
# Run from a local console or wired connection; activating the AP may drop Wi-Fi.
command -v nmcli >/dev/null || { printf 'NetworkManager (nmcli) is required.\n' >&2; exit 1; }
if nmcli -g connection.id connection show pi-module-ap >/dev/null 2>&1; then
    printf '%s\n' 'pi-module-ap already exists; edit it with nmcli instead of overwriting.' >&2
    exit 1
fi
open_ap=false
if [[ "${1:-}" == --open ]]; then open_ap=true; shift; fi
interface="${1:-}"
if [[ -z "$interface" ]]; then
    candidates=()
    while IFS=: read -r device type; do
        if [[ "$type" == wifi ]] && [[ "$(nmcli -g WIFI-PROPERTIES.AP device show "$device" 2>/dev/null)" == yes ]]; then
            candidates+=("$device")
        fi
    done < <(nmcli -t -f DEVICE,TYPE device status)
    if (( ${#candidates[@]} != 1 )); then
        printf 'Found %s AP-capable Wi-Fi interfaces; specify one: bash scripts/configure-hotspot.sh WIFI_INTERFACE\n' "${#candidates[@]}" >&2
        exit 1
    fi
    interface="${candidates[0]}"
fi
if [[ "$(nmcli -g WIFI-PROPERTIES.AP device show "$interface" 2>/dev/null)" != yes ]]; then
    printf 'Wi-Fi interface %s does not support AP mode or is not managed by NetworkManager.\n' "$interface" >&2
    exit 1
fi
if [[ "$open_ap" == false && ! -t 0 ]]; then
    printf 'Run this script in a terminal to enter the WPA2 password interactively.\n' >&2
    exit 1
fi
printf 'Creating PI_MODULE_NET on %s (192.168.50.1/24, channel 6).\n' "$interface"
if [[ "$open_ap" == true ]]; then
    nmcli connection add type wifi ifname "$interface" con-name pi-module-ap ssid PI_MODULE_NET \
        802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel 6 \
        ipv4.method shared ipv4.addresses 192.168.50.1/24 ipv6.method disabled \
        connection.autoconnect yes connection.autoconnect-priority 100
    printf '%s\n' 'Open hotspot profile created (unencrypted). Activation is explicit; secure the management link before enabling it.'
    exit 0
fi
read -r -s -p 'Module AP WPA2 password (8-63 printable ASCII characters): ' ap_password
printf '\n'
export LC_ALL=C
if [[ ! "$ap_password" =~ ^[[:print:]]{8,63}$ ]]; then
    printf 'Invalid WPA2 password (8-63 printable ASCII characters required).\n' >&2
    exit 1
fi
nmcli connection add type wifi ifname "$interface" con-name pi-module-ap ssid PI_MODULE_NET \
    802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel 6 \
    wifi-sec.key-mgmt wpa-psk wifi-sec.proto rsn wifi-sec.psk "$ap_password" \
    ipv4.method shared ipv4.addresses 192.168.50.1/24 ipv6.method disabled \
    connection.autoconnect yes connection.autoconnect-priority 100
unset ap_password
printf '%s\n' 'Hotspot profile created with boot autoconnect enabled. Activating it may replace the current Wi-Fi connection.'
