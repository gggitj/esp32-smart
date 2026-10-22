#!/usr/bin/env python3
"""Check/export the standalone SDK's explicit canonical source dependencies.

No device access, credentials, broad directory sync, or implicit deletion.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "module-devkit"
MANIFEST = KIT / "sdk" / "SOURCE-MANIFEST.json"


def mappings():
    pairs = []
    for name in ("linkit_sdk", "board_pins", "diagnostics"):
        pairs.append((ROOT / "esp/S3_Pi_Module/components" / name,
                      KIT / "sdk/esp32/components" / name))
    for name in ("module_data_service", "module_control_service"):
        pairs.append((ROOT / "esp/shared" / name, KIT / "sdk/esp32/components" / name))
    for name in ("lcd_st7789", "ui_env", "power_guard"):
        pairs.append((ROOT / "esp/S3_Pi_Module/components" / name,
                      KIT / "examples/smart-planter/firmware/components" / name))
    for name in ("pi_sensor_device", "lvgl"):
        pairs.append((ROOT / "esp/shared" / name,
                      KIT / "examples/smart-planter/firmware/components" / name))
    for name in ("main/main.c", "main/Kconfig.projbuild", "partitions.csv"):
        pairs.append((ROOT / "esp/S3_Pi_Module" / name,
                      KIT / "examples/smart-planter/firmware" / name))
    pairs.append((ROOT / "pi_terminal/src/modules/api/pi_module_api.h", KIT / "sdk/include/pi_module_api.h"))
    for name in ("package_tool.py", "mock_module.py"):
        pairs.append((ROOT / "pi_terminal/tools" / name, KIT / "tools" / name))
    return pairs


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--check", action="store_true", help="read-only consistency check")
    modes.add_argument("--write", action="store_true", help="export; refuse unrecorded SDK edits")
    args = parser.parse_args()
    previous = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {"files": {}}
    expected, pending, issues = {}, [], []
    for source, target in mappings():
        if not source.exists():
            issues.append(f"missing canonical source: {source.relative_to(ROOT)}")
            continue
        files = sorted(source.rglob("*")) if source.is_dir() else [source]
        targets = set()
        for f in files:
            if f.is_symlink():
                issues.append(f"symlink source rejected: {f.relative_to(ROOT)}"); continue
            if not f.is_file(): continue
            dest = target / f.relative_to(source) if source.is_dir() else target
            key = dest.relative_to(KIT).as_posix(); targets.add(dest)
            data = f.read_bytes(); sha = digest(data)
            expected[key] = {"source": f.relative_to(ROOT).as_posix(), "sha256": sha}
            if dest.is_symlink():
                issues.append(f"symlink destination rejected: {key}"); continue
            old = digest(dest.read_bytes()) if dest.exists() else None
            if old == sha: continue
            if args.write and (old is None or old == previous["files"].get(key, {}).get("sha256")):
                pending.append((dest, data))
            else:
                issues.append(f"different SDK copy (not overwritten): {key}")
        if source.is_dir() and target.exists():
            for f in target.rglob("*"):
                if f.is_file() and f not in targets:
                    issues.append(f"extra SDK file (not deleted): {f.relative_to(KIT)}")
    if issues:
        for issue in issues: print(issue)
        raise SystemExit(1)
    if args.write:
        for dest, data in pending:
            dest.parent.mkdir(parents=True, exist_ok=True); dest.write_bytes(data)
        MANIFEST.write_text(json.dumps({"format": 1, "files": expected}, ensure_ascii=False, indent=2) + "\n")
    elif not MANIFEST.exists() or previous.get("files") != expected:
        raise SystemExit("source copies match but manifest needs refresh: run --write")
    print(f"SDK canonical export {'updated' if args.write else 'verified'}: {len(expected)} files")


if __name__ == "__main__":
    main()
