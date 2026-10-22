#!/usr/bin/env python3
"""Signed, bounded plugin packages. Runs outside the GUI process; no code is loaded here."""
import argparse
import base64
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import socket
import subprocess
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

LIMIT = 32 * 1024 * 1024
CACHE_LIMIT = 256 * 1024 * 1024
def canonical(obj):
    return json.dumps(obj, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
def sha(data):
    return hashlib.sha256(data).hexdigest()
def safe(name):
    p = PurePosixPath(name)
    if not name or p.is_absolute() or '..' in p.parts or '\\' in name or str(p) != name:
        raise ValueError('unsafe path: ' + name)
    return p
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args):
        raise ValueError('redirects are not allowed')
def download(url, limit):
    if not url.startswith(('http://', 'https://')):
        raise ValueError('expected HTTP(S) package endpoint')
    last_error = None
    for attempt in range(10):
        try:
            with urllib.request.build_opener(NoRedirect).open(url, timeout=2) as response:
                data = response.read(limit + 1)
            if len(data) > limit:
                raise ValueError('download exceeds limit')
            return data
        except urllib.error.HTTPError as error:
            # A module may still be associating to the AP. Retry transient
            # server failures only; redirects and client errors stay rejected.
            if error.code < 500:
                raise
            last_error = error
        except (urllib.error.URLError, TimeoutError, socket.timeout, ConnectionError) as error:
            last_error = error
        if attempt < 9:
            time.sleep(min(5.0, 0.5 * (2 ** attempt)))
    raise ValueError(f'HTTP package download failed after 10 bounded attempts: {last_error}')
def validate_tree(folder, desc):
    actual = set()
    for p in folder.rglob('*'):
        if p.is_symlink(): raise ValueError('symlink in cache')
        if p.is_file():
            relative = p.relative_to(folder).as_posix(); actual.add(relative)
            if sha(p.read_bytes()) != desc['files'].get(relative): raise ValueError('cache content mismatch')
    if actual != set(desc['files']): raise ValueError('file set mismatch')
    manifest = json.loads((folder / 'manifest.json').read_bytes())
    if manifest['module_id'] != desc['module_id'] or manifest['abi_version'] != 1 or desc['arch'] not in manifest['host_arch']:
        raise ValueError('manifest incompatible')
    if any(manifest.get(field) != desc.get(field) for field in ('package_id', 'package_version')):
        raise ValueError('package identity/version mismatch')
    if manifest.get('qt_major') != 6 or manifest.get('unload_contract') != 'synchronous-stop-v1':
        raise ValueError('Qt or lifecycle contract mismatch')
    for key in ('library', 'qml_page'):
        if str(safe(manifest[key])) not in actual: raise ValueError('missing plugin entry')
    # Reject architecture mismatch before dlopen.
    elf = (folder / manifest['library']).read_bytes()[:20]
    machine = {'x86_64': 62, 'aarch64': 183}[desc['arch']]
    if elf[:6] != b'\x7fELF\x02\x01' or int.from_bytes(elf[18:20], 'little') != machine:
        raise ValueError('ELF architecture mismatch')
    return manifest
def verify_descriptor(data, key, module, arch):
    envelope = json.loads(data)
    desc = envelope['descriptor']
    public = serialization.load_pem_public_key(Path(key).read_bytes())
    public.verify(base64.b64decode(envelope['signature'], validate=True), canonical(desc))
    if desc['module_id'] != module or desc['arch'] != arch or desc['abi_version'] != 1:
        raise ValueError('descriptor incompatible')
    for field in ('package_id', 'package_version'):
        if not isinstance(desc.get(field), str) or not 1 <= len(desc[field]) <= 128:
            raise ValueError('missing/invalid ' + field)
    if not 0 < desc['size'] <= LIMIT or not 0 < len(desc['files']) <= 1024:
        raise ValueError('package limits exceeded')
    if len(desc['sha256']) != 64 or any(c not in '0123456789abcdef' for c in desc['sha256']):
        raise ValueError('invalid hash')
    for name in desc['files']: safe(name)
    return desc
def install(url, key, cache, module, arch, fetch=None):
    fetch = fetch or (lambda name, limit: download(url.rstrip('/') + '/' + name, limit))
    desc = verify_descriptor(fetch('package.json', 262144), key, module, arch)
    cache = Path(cache); cache.mkdir(parents=True, exist_ok=True)
    # Keep the cache bounded so repeated signed updates cannot exhaust storage.
    entries = [p for p in cache.iterdir() if p.is_dir() and not p.name.startswith('.incoming-')]
    total = sum(sum(f.stat().st_size for f in p.rglob('*') if f.is_file()) for p in entries)
    for old in sorted(entries, key=lambda p: p.stat().st_atime):
        if total <= CACHE_LIMIT: break
        size = sum(f.stat().st_size for f in old.rglob('*') if f.is_file())
        shutil.rmtree(old, ignore_errors=True); total -= size
    target = cache / desc['sha256']
    if target.exists():
        validate_tree(target, desc)
        return target
    data = fetch('package.tar.zst', desc['size'])
    if len(data) != desc['size'] or sha(data) != desc['sha256']: raise ValueError('package hash/size mismatch')
    with tempfile.TemporaryDirectory(prefix='.incoming-', dir=cache) as temp:
        temp = Path(temp); compressed = temp / 'archive.zst'; compressed.write_bytes(data)
        folder = temp / 'plugin'; folder.mkdir()
        proc = subprocess.Popen(['zstd', '-dc', str(compressed)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            seen = set(); total = 0
            with tarfile.open(fileobj=proc.stdout, mode='r|') as tar:
                for member in tar:
                    safe(member.name)
                    if not member.isfile() or member.name in seen or member.name not in desc['files']:
                        raise ValueError('unexpected/duplicate/non-regular archive entry')
                    seen.add(member.name); total += member.size
                    if total > LIMIT or member.size < 0: raise ValueError('expanded package exceeds limit')
                    p = folder / member.name; p.parent.mkdir(parents=True, exist_ok=True)
                    with tar.extractfile(member) as src, p.open('xb') as dst:
                        shutil.copyfileobj(src, dst, 65536)
            # Drain any remaining decompressed bytes with an independent upper bound.
            remaining = LIMIT
            while chunk := proc.stdout.read(65536):
                remaining -= len(chunk)
                if remaining < 0: raise ValueError('trailing decompressed data exceeds limit')
            if proc.wait(timeout=5): raise ValueError('zstd failure')
        finally:
            if proc.poll() is None: proc.kill(); proc.wait()
            proc.stdout.close()
        validate_tree(folder, desc)
        try: os.rename(folder, target)
        except OSError:
            # Another session may have atomically installed the identical package.
            if not target.is_dir(): raise
            validate_tree(target, desc)
    return target
def local_fetch(folder):
    def fetch(name, limit):
        with (Path(folder) / name).open('rb') as stream: data = stream.read(limit + 1)
        if len(data) > limit: raise ValueError('local file exceeds limit')
        return data
    return fetch
def build(folder, output, key, arch):
    folder, output = Path(folder), Path(output); output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((folder / 'manifest.json').read_bytes())
    files = {}
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode='w') as tar:
        for p in sorted(folder.rglob('*')):
            if p.is_symlink(): raise ValueError('symlink source')
            if p.is_file():
                name = p.relative_to(folder).as_posix(); safe(name)
                files[name] = sha(p.read_bytes()); tar.add(p, arcname=name, recursive=False)
    data = subprocess.run(['zstd', '-q', '-c'], input=stream.getvalue(), stdout=subprocess.PIPE, check=True).stdout
    desc = dict(module_id=manifest['module_id'], package_id=manifest['package_id'], package_version=manifest['package_version'], abi_version=1, arch=arch, size=len(data), sha256=sha(data), files=files)
    private = serialization.load_pem_private_key(Path(key).read_bytes(), password=None)
    output.joinpath('package.tar.zst').write_bytes(data)
    output.joinpath('package.json').write_bytes(canonical(dict(descriptor=desc, signature=base64.b64encode(private.sign(canonical(desc))).decode())))
def main():
    parser = argparse.ArgumentParser(); sub = parser.add_subparsers(dest='command', required=True)
    keys = sub.add_parser('keys'); keys.add_argument('directory')
    pack = sub.add_parser('build'); pack.add_argument('folder'); pack.add_argument('output'); pack.add_argument('key'); pack.add_argument('arch')
    check = sub.add_parser('install'); check.add_argument('url'); check.add_argument('key'); check.add_argument('cache'); check.add_argument('module', type=int); check.add_argument('arch')
    local = sub.add_parser('install-local'); local.add_argument('folder'); local.add_argument('key'); local.add_argument('cache'); local.add_argument('module', type=int); local.add_argument('arch')
    metadata = sub.add_parser('verify-metadata'); metadata.add_argument('folder'); metadata.add_argument('key'); metadata.add_argument('module', type=int); metadata.add_argument('arch')
    args = parser.parse_args()
    if args.command != 'keys' and getattr(args, 'arch', None) == 'arm64': args.arch = 'aarch64'
    if args.command == 'keys':
        path = Path(args.directory); path.mkdir(parents=True, exist_ok=True)
        key = Ed25519PrivateKey.generate()
        with os.fdopen(os.open(path / 'private.pem', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb') as f:
            f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        with (path / 'public.pem').open('xb') as f:
            f.write(key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    elif args.command == 'build': build(args.folder, args.output, args.key, args.arch)
    elif args.command == 'verify-metadata': print(json.dumps(verify_descriptor(local_fetch(args.folder)('package.json', 262144), args.key, args.module, args.arch)))
    elif args.command == 'install-local': print(install('', args.key, args.cache, args.module, args.arch, local_fetch(args.folder)))
    else: print(install(args.url, args.key, args.cache, args.module, args.arch))
if __name__ == '__main__':
    main()
