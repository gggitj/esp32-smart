#!/usr/bin/env python3
"""Run the repository's loopback module transport without hardware."""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools"))
from mock_module import services  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("folder", help="directory containing package.json and package.tar.zst")
parser.add_argument("--module-id", type=int, default=80)
parser.add_argument("--token", default="desktop-test-only")
parser.add_argument("--tcp-port", type=int, default=4580)
parser.add_argument("--http-port", type=int, default=4581)
args = parser.parse_args()
tcp, http = services(args.folder, args.token, args.tcp_port, args.http_port, args.module_id)
print(f"TCP {tcp.server_address} HTTP {http.server_address}", flush=True)
try:
    import threading
    threading.Event().wait()
except KeyboardInterrupt:
    tcp.shutdown(); http.shutdown()
