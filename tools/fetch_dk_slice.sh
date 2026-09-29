#!/usr/bin/env bash
# Downloads one day of Danish historical AIS data and extracts the evaluation
# slice used in docs/tracker-evaluation.md: Class A and B position reports in
# the Oresund strait (55.3-56.2 N, 12.3-13.2 E), 12:00-14:00 on 2026-04-22.
#
# The data is published free of charge by the Danish authorities at
# http://aisdata.ais.dk/. It is not redistributed in this repository; this
# script fetches it and checks the download against a known SHA-256.
#
#   tools/fetch_dk_slice.sh out/oresund-2026-04-22.csv
set -euo pipefail

out=${1:?usage: fetch_dk_slice.sh OUTPUT.csv}
day=2026-04-22
url="http://aisdata.ais.dk/aisdk-${day}.zip"
sha256=31852be42b9488e5893add945d30de7b9af19faca7328822a0f3cd8411ca7cdf

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "downloading $url (about 830 MB)" >&2
curl -fsS --retry 3 -o "$tmp/day.zip" "$url"
echo "$sha256  $tmp/day.zip" | sha256sum -c - >&2

mkdir -p "$(dirname "$out")"
# Columns kept: time, class, MMSI, lat, lon, SOG, COG, heading, ship type.
unzip -p "$tmp/day.zip" | awk -F, '
  NR > 1 && ($2 == "Class A" || $2 == "Class B") {
    split($1, d, /[\/ :]/); h = d[4] + 0
    if (h >= 12 && h < 14 && $4 >= 55.3 && $4 <= 56.2 && $5 >= 12.3 && $5 <= 13.2)
      print $1 "," $2 "," $3 "," $4 "," $5 "," $8 "," $9 "," $10 "," $14
  }' > "$out"
echo "wrote $(wc -l < "$out") reports to $out" >&2
