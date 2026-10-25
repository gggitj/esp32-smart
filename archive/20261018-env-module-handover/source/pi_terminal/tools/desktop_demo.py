#!/usr/bin/env python3
"""Persistent desktop debug harness using real signed packages and TCP sockets."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import signal
import secrets
import threading
import package_tool
from mock_module import services
from mock_bridge import MockBridge


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=root / 'build')
    parser.add_argument('--work', type=Path, default=root / '.desktop')
    parser.add_argument('--serve-only', action='store_true', help='Print config and wait for IDE/debugger')
    args, extra = parser.parse_known_args()
    build, work = args.build.resolve(), args.work.resolve()
    binary = build / 'orange_pi_terminal'
    if not binary.is_file():
        parser.error('Build first: bash scripts/build.sh')
    work.mkdir(parents=True, exist_ok=True)
    plugin = work / 'plugin'; plugin.mkdir(exist_ok=True)
    for name in ('manifest.json', 'libexample_module.so'):
        shutil.copy2(build / 'modules/example' / name, plugin / name)
    shutil.copytree(root / 'modules/example/qml', plugin / 'qml', dirs_exist_ok=True)
    if not (work / 'keys/private.pem').exists():
        subprocess.run([sys.executable, str(root / 'tools/package_tool.py'), 'keys', str(work / 'keys')], check=True)
        (work / 'keys/private.pem').chmod(0o600)
    arch = json.loads((plugin / 'manifest.json').read_text())['host_arch'][0]
    package_tool.build(plugin, work / 'http', work / 'keys/private.pem', arch)
    token = secrets.token_hex(24)
    tcp, http = services(work / 'http', token)
    bridge = MockBridge()
    config = dict(python=sys.executable, package_tool=str(root / 'tools/package_tool.py'),
                  public_key=str(work / 'keys/public.pem'), cache=str(work / 'cache'),
                  modules={'80': dict(transport_mode='fixed_hotspot_lan', address='127.0.0.1',
                     port=tcp.server_address[1], token=token,
                     package_url=f'http://127.0.0.1:{http.server_port}')})
    extra_services=[]
    timers=[]
    # Existing single-module GUI acceptance uses an exact two-tab browser check.
    # Keep that test fixture single-device; normal desktop launches attach all three.
    if '--ui-test' not in extra:
        for module_id,kind in ((129,'environment'),(130,'counter'),(131,'camera')):
            source=build/'modules/test_devices'/kind
            folder=work/kind
            plugin_folder=folder/'plugin'
            plugin_folder.mkdir(parents=True,exist_ok=True)
            for name in ('manifest.json',f'lib{kind}_module.so'):
                shutil.copy2(source/name,plugin_folder/name)
            shutil.copytree(source/'qml',plugin_folder/'qml',dirs_exist_ok=True)
            package_tool.build(plugin_folder,folder/'http',work/'keys/private.pem',arch)
            device,web=services(folder/'http',token,module_id=module_id)
            extra_services.extend((device,web))
            config['modules'][str(module_id)]=dict(transport_mode='fixed_hotspot_lan',address='127.0.0.1',port=device.server_address[1],token=token,package_url=f'http://127.0.0.1:{web.server_port}')
    config_file = work / 'product.json'
    config_file.write_text(json.dumps(config, indent=2), encoding='utf-8')
    config_file.chmod(0o600)
    command = [str(binary), '--config', str(config_file), '--demo', *extra]
    print('Desktop services ready. Run with your debugger:', flush=True)
    print(' '.join(repr(part) for part in command), flush=True)
    print(f'Control UART: {bridge.path}', flush=True)
    process = None
    def stop(signum, frame):
        if process is not None and process.poll() is None: process.terminate()
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    try:
        if args.serve_only:
            signal.pause()
        else:
            process = subprocess.Popen(command, env=dict(os.environ, ORANGE_PI_CONTROL_DEVICE=bridge.path))
            if '--ui-test' in extra:
                # Send a short startup burst while the test process opens its
                # PTY-backed serial port. Repeated attach for this instance is
                # idempotent; keep retries before plugin/data assertions run.
                bridge.attach_later(0.4)
                for delay in (0.7, 1.0):
                    timer=threading.Timer(delay,bridge.emit)
                    timer.daemon=True;timers.append(timer);timer.start()
            else:
                bridge.attach_later()
            for delay,module_id,instance in ((1.2,129,'desktop-environment'),(1.4,130,'desktop-counter'),(1.6,131,'desktop-camera')):
                if str(module_id) in config['modules']:
                    timer=threading.Timer(delay,lambda m=module_id,i=instance:bridge.emit(instance=i,module=m))
                    timer.daemon=True;timers.append(timer);timer.start()
            return process.wait()
    except KeyboardInterrupt:
        if process is not None and process.poll() is None:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        return 0
    finally:
        for timer in timers:timer.cancel();timer.join()
        for server in extra_services:server.shutdown();server.server_close()
        tcp.shutdown(); http.shutdown(); tcp.server_close(); http.server_close()
        bridge.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
