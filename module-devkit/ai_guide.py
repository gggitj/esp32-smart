#!/usr/bin/env python3
"""Generate a project-specific AI coding handoff without copying secrets."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
GUIDE = ROOT / "docs" / "AI-CODING-GUIDE.md"

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("project")
parser.add_argument("--transport", choices=("BLE", "LAN"), required=True)
parser.add_argument("--module-id", type=int, required=True)
parser.add_argument("--output", default="AI-CONTEXT.md")
args = parser.parse_args()
if not 1 <= args.module_id <= 255:
    raise SystemExit("module-id must be 1..255")
project = Path(args.project).resolve()
if not (project / "manifest.json").is_file():
    raise SystemExit(f"not a module project: {project}")
manifest = json.loads((project / "manifest.json").read_text(encoding="utf-8"))
text = f'''# AI Context: {manifest.get("name", "module")}\n\n'''
text += f'''This is a module-devkit project. Work only inside: `{project}`\n\n'''
text += f'''- module_id: `{args.module_id}`\n- transport: `{args.transport}`\n- SDK guide: `{GUIDE}`\n- physical checklist: `{ROOT / "docs" / "PHYSICAL-PI-TEST.md"}`\n\n'''
text += '''Never read or print private keys, tokens, Wi-Fi passwords, or private sdkconfig files.\n\n'''
text += '''Start by asking the developer for hardware pins, commands, payload schema, and acceptance criteria. Then follow AI-CODING-GUIDE.md phase by phase. Do not claim physical success until the checklist is actually executed.\n'''
(project / args.output).write_text(text, encoding="utf-8")
print(project / args.output)
