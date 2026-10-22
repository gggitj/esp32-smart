#!/usr/bin/env python3
"""Loopback-only ESP32 data/package service for desktop acceptance tests."""
import argparse
import base64
import binascii
import hashlib
import json
import math
import socketserver
import struct
import threading
import zlib
from pathlib import Path
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

def wire(e):
    data = json.dumps(e, separators=(',', ':')).encode()
    block = data + struct.pack('<I', binascii.crc32(data))
    return struct.pack('>I', len(block)) + block
def camera_png(frame):
    """Small deterministic moving test chart, no camera hardware required."""
    width,height=320,240
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',binascii.crc32(kind+data))
    colors=((240,240,240),(230,210,40),(30,200,200),(40,190,80),(190,60,200),(200,60,50),(40,80,210),(25,25,25))
    rows=[]
    for y in range(height):
        row=bytearray([0])
        for x in range(width):
            color=colors[x//40]
            if abs(x-(frame*9)%width)<12 and 70<y<170:color=(255,255,255)
            row.extend(color)
        rows.append(row)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')
def exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n-len(data))
        if not chunk: raise EOFError
        data += chunk
    return data
class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        session = None; seq = 0; count = 0; streaming=False
        def send(kind, **body):
            nonlocal seq
            seq += 1
            self.request.sendall(wire(dict(event=kind, module_id=self.server.module_id, module_instance=session, seq=seq, version=1, **body)))
        try:
            self.request.settimeout(10)
            while True:
                n, = struct.unpack('>I', exact(self.request, 4))
                if n < 4 or n > 1048576: return
                block = exact(self.request, n)
                if struct.pack('<I',binascii.crc32(block[:-4])) != block[-4:]: return
                e = json.loads(block[:-4])
                if e['event'] == 'session_open':
                    if e.get('token') != self.server.token or e.get('module_id') != self.server.module_id: return
                    session = e['module_instance']; send('session_ready', token=self.server.token)
                elif not session or e.get('module_instance') != session: return
                elif e['event'] == 'ping': send('pong')
                elif e['event'] == 'package_get':
                    name = e.get('file')
                    if name not in ('package.json', 'package.tar.zst'): return
                    offset, length = e.get('offset'), e.get('length')
                    if not isinstance(offset, int) or offset < 0 or not isinstance(length, int) or not 1 <= length <= 512: return
                    path = Path(self.server.folder) / name
                    with path.open('rb') as stream:
                        stream.seek(offset); chunk = stream.read(length)
                    send('package_chunk', transfer_id=e['transfer_id'], file=name, offset=offset,
                         total=path.stat().st_size, payload=base64.b64encode(chunk).decode())
                elif e['event'] == 'data':
                    command = json.loads(base64.b64decode(e['payload']))['command']
                    if self.server.module_id==131 and command in ('start','stop','frame'):
                        if command=='start':streaming=True
                        elif command=='stop':streaming=False
                        if command!='stop':count+=1
                        sample=dict(frame='data:image/png;base64,'+base64.b64encode(camera_png(count)).decode(),sequence=count,running=streaming,simulated=True)
                        send('data',payload=base64.b64encode(json.dumps(sample).encode()).decode())
                    elif self.server.module_id in (129,130) and command in ('start','stop','reset','increment','decrement'):
                        if command == 'reset': count=0
                        elif command in ('start','increment'): count+=1
                        elif command == 'decrement': count-=1
                        if self.server.module_id==129:
                            sample=dict(temperature=round(24+3*math.sin(count/3),1),humidity=round(50+8*math.cos(count/4),1),sequence=count,status='已停止' if command=='stop' else '已采样',simulated=True)
                        else: sample=dict(value=count,operation=command,simulated=True)
                        send('data',payload=base64.b64encode(json.dumps(sample).encode()).decode())
                    elif command == 'start':
                        send('data', payload=base64.b64encode(b'{"value":42,"unit":"demo"}').decode())
                    elif command == 'request_disconnect':
                        send('module_disconnect_request', reason='user_button', stop_required=True, auto_reconnect=False)
                    elif command == 'drop_link': return
                    elif command == 'stale_data':
                        old = session; session = 'stale-session'
                        send('data', payload=base64.b64encode(b'{"value":"STALE"}').decode()); session = old
                elif e['event'] == 'session_close':
                    send('disconnect_ack', accepted=True, reason='accepted')
                    return
                elif e['event'] == 'disconnect_ack' and e.get('accepted') is True:
                    send('session_close')
                elif e['event'] == 'disconnect_ack' and e.get('accepted') is False:
                    send('data', payload=base64.b64encode(b'{"disconnect_denied":true}').decode())
        except (EOFError, OSError, ValueError, KeyError):
            return
class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
def services(folder, token, tcp_port=0, http_port=0, module_id=80):
    tcp = Server(('127.0.0.1',tcp_port), Handler); tcp.token = token; tcp.folder = folder
    tcp.module_id = module_id
    http = ThreadingHTTPServer(('127.0.0.1',http_port), partial(SimpleHTTPRequestHandler,directory=str(folder)))
    for s in (tcp,http): threading.Thread(target=s.serve_forever,daemon=True).start()
    return tcp,http
if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('folder'); p.add_argument('--token',default='desktop-test-only')
    p.add_argument('--tcp-port',type=int,default=4580); p.add_argument('--http-port',type=int,default=4581)
    a = p.parse_args(); tcp,http = services(a.folder,a.token,a.tcp_port,a.http_port)
    print('TCP',tcp.server_address,'HTTP',http.server_address,flush=True)
    threading.Event().wait()
