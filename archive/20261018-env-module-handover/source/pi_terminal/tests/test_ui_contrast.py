#!/usr/bin/env python3
"""WCAG relative luminance, evaluated on authored colors (not antialiased pixels)."""
import json
from pathlib import Path
import re
import sys

root=Path(__file__).resolve().parents[1]
theme=dict(re.findall(r'property color (\w+):\s*"(#[0-9a-fA-F]{6})"',(root/'qml/Theme.qml').read_text()))
def rgb(value):
    return [int(value[n:n+2],16)/255 for n in (1,3,5)]
def luminance(value):
    linear=[c/12.92 if c<=0.04045 else ((c+0.055)/1.055)**2.4 for c in rgb(value)]
    return sum(a*b for a,b in zip(linear,(0.2126,0.7152,0.0722)))
def contrast(fg,bg):
    a,b=sorted((luminance(fg),luminance(bg)))
    return (b+0.05)/(a+0.05)
def blend(fg,bg,alpha):
    return '#'+''.join(f'{round((a*alpha+b*(1-alpha))*255):02x}' for a,b in zip(rgb(fg),rgb(bg)))

pairs=[]
for foreground in ('foreground','secondary','accent'):
    for background in ('background','surface','raised','pressed'):
        pairs.append((foreground+'/'+background,theme[foreground],theme[background]))
pairs += [('accent button',theme['accentInk'],theme['accent']),('disabled button',theme['disabledText'],theme['disabledSurface'])]
for accent in ('#85deff','#a5e6c4','#ffdaa0','#c9baff'):
    pairs.append(('card icon '+accent,theme['accentInk'],accent))
    for fg in ('foreground','secondary'):
        pairs.append(('card decoration '+accent+'/'+fg,theme[fg],blend(accent,theme['surface'],0.12)))
report=[dict(pair=name,foreground=fg,background=bg,ratio=contrast(fg,bg)) for name,fg,bg in pairs]
failures=[row for row in report if row['ratio']<4.5]
assert not failures, json.dumps(failures,indent=2)
source='\n'.join(p.read_text() for base in ('src','qml') for p in (root/base).rglob('*') if p.suffix in ('.qml','.cpp','.h'))
assert 'QGraphicsBlurEffect' not in source
assert re.search(r'axis.x:\s*0;\s*axis.y:\s*0;\s*axis.z:\s*1',(root/'qml/AppLauncher.qml').read_text())
assert len(list((root/'qml/icons/material/dark').glob('*.svg'))) >= 25
if len(sys.argv)>1:
    Path(sys.argv[1]).write_text(json.dumps(dict(minimum=min(row['ratio'] for row in report),pairs=report),indent=2))
print(f'PASS: {len(report)} text color pairs; minimum {min(row["ratio"] for row in report):.3f}:1; Z axis and GPU-only blur source checks')
