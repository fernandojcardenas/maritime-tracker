#!/usr/bin/env python3
"""Serve a recorded NMEA file over TCP like a live AIS feed, with faults.

Each connection gets the next --per-connection lines, sent in chunks that
split lines in half (as real TCP reads do), then the server drops the
connection. Use it to exercise mt-ingest's reconnect path locally:

    python3 tools/fake_feed.py testdata/pyais-nmea-sample.nmea --connections 3 &
    ./build/mt-ingest --tcp 127.0.0.1:5631 --record /tmp/rec --duration 8
"""

import argparse
import socket
import time


def main():
    p = argparse.ArgumentParser()
    p.add_argument("file")
    p.add_argument("--port", type=int, default=5631)
    p.add_argument("--connections", type=int, default=3)
    p.add_argument("--per-connection", type=int, default=3000)
    p.add_argument("--batch", type=int, default=37, help="lines per send")
    p.add_argument("--interval", type=float, default=0.01, help="seconds between sends")
    args = p.parse_args()

    lines = open(args.file, "rb").read().split(b"\n")
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.port))
    srv.listen(4)

    pos = 0
    sent = 0
    for n in range(args.connections):
        conn, _ = srv.accept()
        end = pos + args.per_connection
        try:
            while pos < end and pos < len(lines):
                batch = lines[pos:pos + args.batch]
                chunk = b"\n".join(batch) + b"\n"
                half = len(chunk) // 2
                conn.sendall(chunk[:half])
                time.sleep(0.002)
                conn.sendall(chunk[half:])
                pos += len(batch)
                sent += len(batch)
                time.sleep(args.interval)
        except (BrokenPipeError, ConnectionResetError):
            print(f"connection {n + 1}: client disconnected", flush=True)
        conn.close()  # drop the connection mid-stream
        print(f"connection {n + 1}: closed after {sent} lines total", flush=True)
        time.sleep(0.3)
    srv.close()


if __name__ == "__main__":
    main()
