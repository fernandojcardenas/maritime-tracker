#!/usr/bin/env bash
# Starts mt-ingest with the live map on port 8080 (all interfaces, since this
# runs in a container; publish it with -p 8080:8080).
#
# With BW_CLIENT_ID and BW_CLIENT_SECRET set, it tracks the BarentsWatch Live
# AIS stream. Otherwise it replays the bundled sample (5,000 real records,
# about two minutes of Norwegian AIS) at SPEED (default 1, real time) and
# keeps serving the map after the replay ends. Extra arguments go to mt-ingest.
set -euo pipefail

serve=(--serve "${PORT:-8080}" --serve-address 0.0.0.0 --stats-every "${STATS_EVERY:-30}")

if [[ -n "${BW_CLIENT_ID:-}" && -n "${BW_CLIENT_SECRET:-}" ]]; then
    echo "entrypoint: live mode (BarentsWatch Live AIS)" >&2
    # mt-ingest is PID 1 so it receives docker stop's SIGTERM; the stream
    # script exits when its reader goes away.
    exec mt-ingest --stdin --format barentswatch "${serve[@]}" "$@" < <(barentswatch_stream.sh)
fi

echo "entrypoint: replaying the bundled sample (set BW_CLIENT_ID and BW_CLIENT_SECRET for live data)" >&2
exec mt-ingest --replay /usr/share/maritime-tracker/sample.jsonl --format barentswatch \
    --speed "${SPEED:-1}" "${serve[@]}" "$@"
