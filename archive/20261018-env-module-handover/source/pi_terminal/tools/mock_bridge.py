#!/usr/bin/env python3
"""PBR1 USB/UART simulator backed by a real Linux pseudo-terminal."""
import binascii
import json
import os
import pty
import struct
import threading


def frame(event, sequence=1):
    payload = json.dumps(event, separators=(',', ':')).encode()
    body = struct.pack('<BBIH', 1, 1, sequence, len(payload)) + payload
    return b'PBR1' + body + struct.pack('<H', binascii.crc_hqx(body, 0xffff))


class MockBridge:
    def __init__(self):
        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)
        self.sequence = 0
        self.lock = threading.Lock()
        self.timer = None

    def emit(self, event='attach', instance='desktop-demo', module=80):
        with self.lock:
            self.sequence += 1
            os.write(self.master, frame(dict(event=event, module_id=module, module_instance=instance), self.sequence))

    def attach_later(self, seconds=1):
        self.timer = threading.Timer(seconds, self.emit)
        self.timer.daemon = True
        self.timer.start()

    def close(self):
        if self.timer:
            self.timer.cancel(); self.timer.join()
        os.close(self.master); os.close(self.slave)
