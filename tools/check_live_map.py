#!/usr/bin/env python3
"""Checks a running live map from outside, as a browser would.

    tools/check_live_map.py http://127.0.0.1:8080/ [--min-tracks N] [--updates N]

Fetches the page and its files, opens the WebSocket (standard library only,
no client package), and checks that the first message is a snapshot and that
update messages follow: at least --updates of them, and until the feed
reports at least --min-tracks vessels (within --timeout seconds). Exits 1
with a reason on the first failure. Used by the CI docker job.
"""
import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time
import urllib.parse
import urllib.request

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def fail(msg):
    print(f"check_live_map: FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def fetch(url):
    with urllib.request.urlopen(url, timeout=10) as r:
        return r.status, r.headers, r.read()


def recv_exact(sock, n):
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            fail("connection closed by the server")
        data += chunk
    return data


def recv_message(sock):
    """One complete text message (the server sends unfragmented frames)."""
    b0, b1 = recv_exact(sock, 2)
    opcode, length = b0 & 0x0F, b1 & 0x7F
    if b1 & 0x80:
        fail("server frames must not be masked")
    if length == 126:
        (length,) = struct.unpack("!H", recv_exact(sock, 2))
    elif length == 127:
        (length,) = struct.unpack("!Q", recv_exact(sock, 8))
    payload = recv_exact(sock, length)
    if opcode == 8:
        fail("server sent close")
    if opcode != 1 or not b0 & 0x80:
        fail(f"unexpected frame: opcode {opcode}, fin {bool(b0 & 0x80)}")
    return json.loads(payload)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("--min-tracks", type=int, default=1)
    ap.add_argument("--updates", type=int, default=2)
    ap.add_argument("--timeout", type=float, default=30)
    args = ap.parse_args()
    base = args.url if args.url.endswith("/") else args.url + "/"

    status, headers, body = fetch(base)
    if status != 200 or b"maritime-tracker" not in body:
        fail(f"page: HTTP {status}")
    if "default-src 'self'" not in headers.get("Content-Security-Policy", ""):
        fail("page has no Content-Security-Policy")
    for path in ["app.js", "style.css", "land.json", "vendor/leaflet/leaflet.js", "vendor/leaflet/leaflet.css"]:
        status, _, data = fetch(base + path)
        if status != 200 or not data:
            fail(f"{path}: HTTP {status}, {len(data)} bytes")
    print(f"check_live_map: page and files OK ({base})")

    u = urllib.parse.urlparse(base)
    key = base64.b64encode(os.urandom(16)).decode()
    sock = socket.create_connection((u.hostname, u.port or 80), timeout=args.timeout)
    sock.sendall((f"GET /ws HTTP/1.1\r\nHost: {u.netloc}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    head = b""
    while b"\r\n\r\n" not in head:
        head += recv_exact(sock, 1)
    lines = head.decode().split("\r\n")
    if not lines[0].startswith("HTTP/1.1 101"):
        fail(f"upgrade refused: {lines[0]}")
    want = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
    if f"Sec-WebSocket-Accept: {want}" not in lines:
        fail("wrong Sec-WebSocket-Accept")

    snap = recv_message(sock)
    if snap.get("type") != "snapshot":
        fail(f"first message is {snap.get('type')!r}, not a snapshot")
    print(f"check_live_map: snapshot OK: {len(snap['tracks'])} tracks, "
          f"{snap['totals']['messages']} messages, {len(snap['encounters'])} encounters")
    deadline = time.monotonic() + args.timeout
    updates, tracks = 0, len(snap["tracks"])
    while updates < args.updates or tracks < args.min_tracks:
        if time.monotonic() > deadline:
            fail(f"after {args.timeout:.0f} s: {updates} updates, {tracks} tracks "
                 f"(wanted {args.updates} and {args.min_tracks})")
        m = recv_message(sock)
        if m.get("type") != "update":
            fail(f"message {updates + 2} is {m.get('type')!r}, not an update")
        updates += 1
        tracks = m["totals"]["tracks"]
        print(f"check_live_map: update {updates} OK: {len(m['tracks'])} changed tracks, "
              f"{tracks} tracks, {m['totals']['messages']} messages")
    sock.close()


if __name__ == "__main__":
    main()
