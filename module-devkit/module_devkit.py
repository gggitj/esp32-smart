#!/usr/bin/env python3
"""Create and validate host modules without loading native code.

This is intentionally a thin developer-facing wrapper around the repository's
existing C ABI and package_tool.py. It never flashes hardware or installs a
module into a running terminal.
"""
import argparse
import json
import re
import shutil
import subprocess
import platform
from pathlib import Path

KIT = Path(__file__).resolve().parent
ROOT = KIT.parent
TEMPLATE = KIT / "template"
REQUIRED = ("id", "name", "version", "module_id", "package_id", "package_version",
            "host_arch", "library", "qml_page", "abi_version", "min_host_api",
            "qt_major", "unload_contract")

def load_manifest(folder, require_binary=True):
    path = Path(folder) / "manifest.json"
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid manifest.json: {exc}")
    missing = [key for key in REQUIRED if key not in value]
    if missing: raise SystemExit("manifest missing: " + ", ".join(missing))
    if not re.fullmatch(r"[a-z0-9][a-z0-9._-]*", value["id"]):
        raise SystemExit("manifest id must contain lowercase letters, digits, '.', '_' or '-'")
    if not isinstance(value["module_id"], int) or not 1 <= value["module_id"] <= 255:
        raise SystemExit("module_id must be an integer from 1 to 255")
    if value["abi_version"] != 1 or value["min_host_api"] < 1:
        raise SystemExit("only ABI 1 and host API >= 1 are supported")
    if value["qt_major"] != 6 or value["unload_contract"] != "synchronous-stop-v1":
        raise SystemExit("module must target Qt 6 and synchronous-stop-v1")
    for key in ("library", "qml_page"):
        relative = Path(value[key])
        if relative.is_absolute() or ".." in relative.parts:
            raise SystemExit(f"manifest path escapes package: {value[key]}")
        if key == "library" and not require_binary:
            continue
        path = Path(folder) / relative
        candidates = [path]
        if key == "library":
            candidates.extend((Path(folder) / "build" / relative,
                               Path(folder) / "build" / ("lib" + relative.name
                               if not relative.name.startswith("lib") else relative.name)))
        if not any(candidate.is_file() for candidate in candidates):
            raise SystemExit(f"manifest entry does not exist: {value[key]}")
    return value

def init_project(destination, module_id, name):
    destination = Path(destination)
    if destination.exists() and any(destination.iterdir()):
        raise SystemExit(f"destination is not empty: {destination}")
    destination.mkdir(parents=True, exist_ok=True)
    for source in TEMPLATE.rglob("*"):
        if source.is_file():
            target = destination / source.relative_to(TEMPLATE)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(source.read_text(encoding="utf-8").replace("@MODULE_ID@", str(module_id)).replace("@MODULE_NAME@", name), encoding="utf-8")
    print(f"created {destination}")

def stage_project(project, build, destination, arch):
    """Prepare an unsigned package; signing may stay on a separate machine."""
    project, build, destination = Path(project).resolve(), Path(build).resolve(), Path(destination).resolve()
    manifest = load_manifest(project, require_binary=False)
    arch = "aarch64" if arch == "arm64" else arch
    if arch not in ("aarch64", "x86_64") or arch not in manifest["host_arch"]:
        raise SystemExit("target architecture must be declared in host_arch")
    library = build / manifest["library"]
    if not library.is_file() or library.is_symlink(): raise SystemExit("missing/non-regular built library")
    elf = library.read_bytes()[:20]
    if elf[:6] != b'\x7fELF\x02\x01' or int.from_bytes(elf[18:20], 'little') != {"aarch64": 183, "x86_64": 62}[arch]:
        raise SystemExit("built ELF architecture differs from --arch; refusing to package")
    if destination.exists() and any(destination.iterdir()): raise SystemExit("staging directory must be empty")
    qml = project / "qml"
    if qml.is_symlink() or any(p.is_symlink() for p in qml.rglob("*")):
        raise SystemExit("symlink QML resources are not allowed")
    destination.mkdir(parents=True, exist_ok=True)
    target = destination / manifest["library"]; target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(library, target)
    shutil.copyfile(project / "manifest.json", destination / "manifest.json")
    shutil.copytree(qml, destination / "qml")
    load_manifest(destination)
    print(f"unsigned package ready: {destination}")

def release_project(project, output, key, arch, toolchain=None, firmware_mode=None, sdkconfig=None):
    project, output = Path(project).resolve(), Path(output).resolve()
    manifest = load_manifest(project, require_binary=False)
    if arch == "native":
        arch = "aarch64" if platform.machine() in ("aarch64", "arm64") else "x86_64"
    if arch not in ("aarch64", "x86_64"):
        raise SystemExit("--arch must be native, aarch64, or x86_64")
    if arch not in manifest["host_arch"] and not (arch == "aarch64" and "arm64" in manifest["host_arch"]):
        raise SystemExit(f"manifest host_arch does not allow {arch}")
    if arch == "aarch64" and not toolchain:
        raise SystemExit("ARM64 release requires --toolchain; refusing to package a native host library")
    build = output / "host-build"
    command = ["cmake", "-S", str(project), "-B", str(build), "-DMODULE_SDK_DIR=" + str(KIT / "sdk")]
    if toolchain: command.append("-DCMAKE_TOOLCHAIN_FILE=" + str(Path(toolchain).resolve()))
    subprocess.run(command, check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel"], check=True)
    package_folder = output / "package"
    stage_project(project, build, package_folder, arch)
    signed = output / "signed-package"
    subprocess.run(["python3", str(KIT / "tools" / "package_tool.py"), "build",
                    str(package_folder), str(signed), str(Path(key).resolve()), arch], check=True)
    firmware_package = output / "firmware-package"
    subprocess.run(["python3", str(KIT / "module_devkit.py"), "prepare-firmware",
                    str(signed), str(firmware_package)], check=True)
    if firmware_mode:
        if not sdkconfig: raise SystemExit("--sdkconfig is required with --firmware-mode")
        env = dict(__import__("os").environ, PI_MODULE_SDKCONFIG=str(Path(sdkconfig).resolve()),
                   PI_MODULE_PACKAGE_DIR=str(firmware_package))
        subprocess.run([str(KIT / "firmware-build.sh"), str(project), firmware_mode,
                        str(output / ("firmware-" + firmware_mode.lower())),], env=env, check=True)
    print(f"release ready: {output}")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    init = sub.add_parser("init", help="create a minimal C++ module")
    init.add_argument("destination"); init.add_argument("--module-id", type=int, required=True); init.add_argument("--name", required=True)
    check = sub.add_parser("validate", help="validate manifest and referenced files")
    check.add_argument("folder")
    check.add_argument("--require-binary", action="store_true", help="also require the built shared library")
    pack = sub.add_parser("package", help="build a signed package using package_tool.py")
    pack.add_argument("folder"); pack.add_argument("output"); pack.add_argument("key"); pack.add_argument("--arch", default="aarch64")
    embed = sub.add_parser("prepare-firmware", help="copy a signed host package for ESP embedding")
    embed.add_argument("package"); embed.add_argument("firmware_dir")
    sub.add_parser("doctor", help="check host-side SDK tools")
    stage = sub.add_parser("stage", help="collect library + all QML resources for separate signing")
    stage.add_argument("project"); stage.add_argument("build"); stage.add_argument("output")
    stage.add_argument("--arch", choices=("aarch64", "x86_64", "arm64"), required=True)
    release = sub.add_parser("release", help="build host package and optionally ESP32 firmware")
    release.add_argument("project"); release.add_argument("output"); release.add_argument("key")
    release.add_argument("--arch", default="native"); release.add_argument("--toolchain")
    release.add_argument("--firmware-mode", choices=("BLE", "LAN")); release.add_argument("--sdkconfig")
    args = parser.parse_args()
    if args.command == "stage":
        stage_project(args.project, args.build, args.output, args.arch)
        return
    if args.command == "doctor":
        checks = [("python3", ["python3", "--version"]), ("cmake", ["cmake", "--version"]),
                  ("c++", ["c++", "--version"]), ("zstd", ["zstd", "--version"])]
        failed = False
        for name, command in checks:
            try: subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            except (OSError, subprocess.CalledProcessError): print(f"MISSING {name}"); failed = True
            else: print(f"OK      {name}")
        try: import cryptography  # noqa: F401
        except ImportError: print("MISSING python package cryptography"); failed = True
        else: print("OK      python package cryptography")
        try: subprocess.run(["idf.py", "--version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        except (OSError, subprocess.CalledProcessError): print("INFO    idf.py (required only for firmware builds)")
        else: print("OK      idf.py")
        raise SystemExit(1 if failed else 0)
    if args.command == "release":
        release_project(args.project, args.output, args.key, args.arch, args.toolchain,
                        args.firmware_mode, args.sdkconfig)
        return
    if args.command == "init": init_project(args.destination, args.module_id, args.name); return
    if args.command == "prepare-firmware":
        source, target = Path(args.package), Path(args.firmware_dir)
        target.mkdir(parents=True, exist_ok=True)
        for source_name, target_name in (("package.json", "embedded_package.json"), ("package.tar.zst", "embedded_package.tar.zst")):
            source_file = source / source_name
            if not source_file.is_file(): raise SystemExit(f"missing signed package file: {source_file}")
            shutil.copyfile(source_file, target / target_name)
        print(f"prepared firmware package in {target}")
        return
    manifest = load_manifest(args.folder, require_binary=getattr(args, "require_binary", False) or args.command == "package")
    if args.command == "validate": print(json.dumps(manifest, ensure_ascii=False, indent=2)); return
    tool = KIT / "tools" / "package_tool.py"
    subprocess.run(["python3", str(tool), "build", args.folder, args.output, args.key, args.arch], check=True)

if __name__ == "__main__": main()
