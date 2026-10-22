#!/usr/bin/env python3
"""Vendor Google's Apache-2.0 Material Icons Round SVGs; no runtime downloads."""
from pathlib import Path
import hashlib
import json
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1] / 'qml/icons/material'
ICONS = {
    'apps':'navigation', 'language':'action', 'folder':'file', 'settings':'action', 'minimize':'content',
    'extension':'action', 'close':'navigation', 'arrow_back':'navigation',
    'chevron_left':'navigation', 'chevron_right':'navigation',
    'notifications':'social', 'wifi':'notification', 'wifi_off':'notification',
    'lan':'device', 'bluetooth':'device', 'bluetooth_disabled':'device',
    'battery_full':'device', 'battery_unknown':'device', 'power':'notification',
    'tune':'image', 'volume_up':'av', 'volume_off':'av', 'memory':'hardware',
    'storage':'device', 'refresh':'navigation', 'check':'navigation'
}
ICONS.update({f'battery_{level}_bar':'device' for level in range(7)})
ICONS['battery_charging_full']='device'

def main():
    for variant in ('dark','light'): (ROOT / variant).mkdir(parents=True,exist_ok=True)
    manifest = {}
    for name,category in ICONS.items():
        url=f'https://raw.githubusercontent.com/google/material-design-icons/master/src/{category}/{name}/materialiconsround/24px.svg'
        original=urllib.request.urlopen(url,timeout=30).read()
        assert ET.fromstring(original).tag.endswith('svg'), name
        manifest[name]={'source':url,'sha256':hashlib.sha256(original).hexdigest()}
        for variant,color in (('dark','#081b29'),('light','#f4f8ff')):
            svg=original.decode().replace('<svg ',f'<svg fill="{color}" ',1)
            (ROOT/variant/f'{name}.svg').write_text('<!-- Google Material Icons Round; color modified for terminal theme; glyph unchanged. Apache-2.0. -->\n'+svg+'\n')
    (ROOT/'SOURCES.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Vendored {len(manifest)} Material Icons Round glyphs in two color variants.')

if __name__=='__main__': main()
