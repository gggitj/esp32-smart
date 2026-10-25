#!/usr/bin/env python3
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
import package_tool
from mock_module import services
from mock_bridge import MockBridge

def main():
    p=argparse.ArgumentParser(); p.add_argument('build'); p.add_argument('--gui',action='store_true'); args=p.parse_args()
    build=Path(args.build).resolve()
    with tempfile.TemporaryDirectory(prefix='terminal-integration-') as tmp:
        tmp=Path(tmp); plugin=tmp/'plugin'; plugin.mkdir()
        for name in ('manifest.json','libexample_module.so'):
            shutil.copy(build/'modules/example'/name,plugin/name)
        shutil.copytree(root/'modules/example/qml',plugin/'qml')
        subprocess.run([sys.executable,str(root/'tools/package_tool.py'),'keys',str(tmp/'keys')],check=True)
        arch=json.loads((plugin/'manifest.json').read_text())['host_arch'][0]
        package_tool.build(plugin,tmp/'http',tmp/'keys/private.pem',arch)
        tcp,http=services(tmp/'http','desktop-test-only')
        url=f'http://127.0.0.1:{http.server_port}'
        config=dict(python=sys.executable,package_tool=str(root/'tools/package_tool.py'),public_key=str(tmp/'keys/public.pem'),cache=str(tmp/'cache'),
                    modules={'80':dict(transport_mode='fixed_hotspot_lan',address='127.0.0.1',port=tcp.server_address[1],token='desktop-test-only',package_url=url)})
        (tmp/'config.json').write_text(json.dumps(config))
        try:
            env=dict(os.environ,TEST_CONFIG=str(tmp/'config.json'),TEST_PLUGIN=str(plugin))
            subprocess.run([str(build/'session_test')],env=env,check=True,timeout=60)
            # Exercise exactly the small-package wire protocol used by BLE over
            # deterministic loopback TCP, independently of the HTTP downloader.
            config['modules']['80']['package_transport'] = 'data'
            config['cache'] = str(tmp/'chunk-cache')
            (tmp/'config.json').write_text(json.dumps(config))
            subprocess.run([str(build/'session_test'), 'sessions'],env=env,check=True,timeout=60)
            del config['modules']['80']['package_transport']
            config['cache'] = str(tmp/'cache')
            (tmp/'config.json').write_text(json.dumps(config))
            # Signed metadata, archive tamper, cache tamper and incompatible architecture.
            target=package_tool.install(url,tmp/'keys/public.pem',tmp/'cache',80,arch)
            (target/'qml/ExamplePage.qml').write_text('tampered')
            try: package_tool.install(url,tmp/'keys/public.pem',tmp/'cache',80,arch)
            except ValueError: pass
            else: raise AssertionError('cache tamper accepted')
            shutil.rmtree(target)
            envelope=json.loads((tmp/'http/package.json').read_text()); envelope['descriptor']['module_id']=129
            (tmp/'http/package.json').write_text(json.dumps(envelope))
            try: package_tool.install(url,tmp/'keys/public.pem',tmp/'cache',129,arch)
            except Exception: pass
            else: raise AssertionError('invalid signature accepted')
            package_tool.build(plugin,tmp/'http',tmp/'keys/private.pem',arch)
            archive=tmp/'http/package.tar.zst'; archive.write_bytes(archive.read_bytes()+b'x')
            try: package_tool.install(url,tmp/'keys/public.pem',tmp/'cache',80,arch)
            except ValueError: pass
            else: raise AssertionError('archive tamper accepted')
            package_tool.build(plugin,tmp/'http',tmp/'keys/private.pem',arch)
            if args.gui:
                bridge = MockBridge()
                try:
                    proc = subprocess.Popen(['xvfb-run','-a',str(build/'orange_pi_terminal'),'--config',str(tmp/'config.json'),'--ui-test','--quit-after','7000',
                                    '--screenshot',str(build/'demo.png')],env=dict(env, ORANGE_PI_CONTROL_DEVICE=bridge.path),stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                    bridge.attach_later(1.5)
                    try: stdout, stderr = proc.communicate(timeout=25)
                    except subprocess.TimeoutExpired:
                        proc.kill(); proc.communicate(); raise
                    assert proc.returncode == 0, stderr
                    combined = stdout + stderr
                finally: bridge.close()
                print(combined)
                assert 'UI_TEST_PASS' in combined, 'UI test did not finish'
                assert 'TypeError' not in combined and 'ReferenceError' not in combined, combined
            print('PASS: session flow, detach preservation, cache reuse, 100 real library cycles, tamper rejection')
        finally:
            tcp.shutdown();http.shutdown();tcp.server_close();http.server_close()
if __name__=='__main__': main()
