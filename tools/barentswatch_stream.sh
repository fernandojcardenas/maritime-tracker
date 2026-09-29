#!/usr/bin/env bash
# Streams the BarentsWatch Live AIS API (Norwegian open AIS data, NLOD
# licence) to stdout as one JSON record per line, for mt-ingest:
#
#   export BW_CLIENT_ID=... BW_CLIENT_SECRET=...     # from barentswatch.no
#   tools/barentswatch_stream.sh | ./build/mt-ingest --stdin --format barentswatch
#
# It gets an OAuth2 access token (client credentials, scope "ais"), opens
# the stream, and when the stream ends (tokens expire after about an hour,
# networks drop) gets a new token and reconnects with backoff. It exits when
# its reader goes away. Credentials are read from the environment and never
# printed or passed on a command line.
set -uo pipefail

: "${BW_CLIENT_ID:?set BW_CLIENT_ID (see docs/data-sources.md)}"
: "${BW_CLIENT_SECRET:?set BW_CLIENT_SECRET (see docs/data-sources.md)}"
token_url=${BW_TOKEN_URL:-https://id.barentswatch.no/connect/token}
stream_url=${BW_STREAM_URL:-https://live.ais.barentswatch.no/v1/combined}

work=$(mktemp -d)
chmod 700 "$work"
trap 'rm -rf "$work"' EXIT
log() { echo "barentswatch_stream: $*" >&2; }

get_token() {
    # Form body built from the environment, so the secret never appears in argv.
    python3 -c 'import os, urllib.parse; print(urllib.parse.urlencode({
        "client_id": os.environ["BW_CLIENT_ID"], "client_secret": os.environ["BW_CLIENT_SECRET"],
        "scope": "ais", "grant_type": "client_credentials"}), end="")' > "$work/body"
    if ! curl -fsS --max-time 30 -H "Content-Type: application/x-www-form-urlencoded" \
            --data-binary @"$work/body" "$token_url" > "$work/token.json"; then
        return 1
    fi
    python3 -c 'import json, sys; t = json.load(open(sys.argv[1])).get("access_token", "")
if not t: sys.exit(1)
print("Authorization: Bearer " + t)' "$work/token.json" > "$work/auth" || return 1
    chmod 600 "$work/auth"
}

backoff=2
while true; do
    started=$(date +%s)
    if get_token; then
        log "token ok, connecting to $stream_url"
        curl -fsS -N --no-buffer -H @"$work/auth" "$stream_url"
        code=$?
        if [ "$code" -eq 23 ]; then
            log "reader closed the pipe, stopping"
            exit 0
        fi
        log "stream ended (curl exit $code) after $(( $(date +%s) - started )) s"
    else
        log "could not get a token (check BW_CLIENT_ID / BW_CLIENT_SECRET)"
    fi
    # A stream that ran for a while resets the backoff; quick failures back off.
    if [ $(( $(date +%s) - started )) -gt 60 ]; then backoff=2; fi
    log "reconnecting in ${backoff} s"
    sleep "$backoff"
    backoff=$(( backoff * 2 > 60 ? 60 : backoff * 2 ))
done
