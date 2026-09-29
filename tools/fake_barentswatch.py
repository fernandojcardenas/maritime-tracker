#!/usr/bin/env python3
"""A local stand-in for the BarentsWatch token and live AIS endpoints.

Serves real vessel positions from a Danish AIS slice (see
tools/fetch_dk_slice.sh) in the BarentsWatch record format, one JSON object
per line, and ends each stream after --per-stream records, the way the real
stream ends when a token expires. Used to test tools/barentswatch_stream.sh
and `mt-ingest --stdin --format barentswatch` without credentials:

    python3 tools/fake_barentswatch.py data/oresund.csv --port 8765 &
    BW_CLIENT_ID=test BW_CLIENT_SECRET=secret \\
    BW_TOKEN_URL=http://127.0.0.1:8765/connect/token \\
    BW_STREAM_URL=http://127.0.0.1:8765/v1/combined \\
      tools/barentswatch_stream.sh | ./build/mt-ingest --stdin --format barentswatch --duration 10
"""

import argparse
import datetime
import json
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TOKEN = "fake-token-123"


def records(csv_path):
    for row in open(csv_path):
        c = row.rstrip("\n").split(",")
        if len(c) < 7:
            continue
        t = datetime.datetime.strptime(c[0], "%d/%m/%Y %H:%M:%S").replace(tzinfo=datetime.timezone.utc)
        rec = {"mmsi": int(c[2]), "latitude": float(c[3]), "longitude": float(c[4]),
               "msgtime": t.isoformat()}
        if c[5] and c[6]:
            rec["speedOverGround"] = float(c[5])
            rec["courseOverGround"] = float(c[6])
        yield json.dumps(rec)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("--port", type=int, default=8765)
    p.add_argument("--per-stream", type=int, default=2000)
    p.add_argument("--rate", type=float, default=2000.0, help="records per second")
    args = p.parse_args()

    source = records(args.csv)
    lock = threading.Lock()
    stats = {"tokens": 0, "streams": 0, "unauthorized": 0}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_POST(self):
            body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
            form = urllib.parse.parse_qs(body)
            ok = (self.path == "/connect/token" and form.get("grant_type") == ["client_credentials"]
                  and form.get("scope") == ["ais"] and form.get("client_id") and form.get("client_secret"))
            if not ok:
                self.send_response(400)
                self.end_headers()
                return
            stats["tokens"] += 1
            out = json.dumps({"access_token": TOKEN, "expires_in": 3600, "token_type": "Bearer"}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(out)))
            self.end_headers()
            self.wfile.write(out)

        def do_GET(self):
            if self.headers.get("Authorization") != "Bearer " + TOKEN:
                stats["unauthorized"] += 1
                self.send_response(401)
                self.end_headers()
                return
            stats["streams"] += 1
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            try:
                for _ in range(args.per_stream):
                    with lock:
                        line = next(source, None)
                    if line is None:
                        break
                    self.wfile.write((line + "\n").encode())
                    self.wfile.flush()
                    time.sleep(1.0 / args.rate)
            except (BrokenPipeError, ConnectionResetError):
                pass
            print(f"stream {stats['streams']} ended; tokens issued {stats['tokens']}, "
                  f"unauthorized requests {stats['unauthorized']}", flush=True)

    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
