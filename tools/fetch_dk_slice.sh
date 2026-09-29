#!/usr/bin/env bash
# Downloads one day of Danish historical AIS data and extracts the evaluation
# slices, Class A and B position reports only:
#
#   Øresund strait     55.3-56.2 N, 12.3-13.2 E, 12:00-14:00 UTC  (tracker evaluation; anomaly rules developed here)
#   Great Belt         55.0-55.8 N, 10.6-11.4 E, 16:00-18:00 UTC  (anomaly evaluation)
#   Skagen             57.3-58.0 N, 10.0-11.2 E, 06:00-08:00 UTC  (anomaly evaluation)
#   Bornholm           54.9-55.4 N, 14.5-15.3 E, 20:00-22:00 UTC  (anomaly evaluation, final hold-out)
#
# all on 2026-04-22. The data is published free of charge by the Danish
# authorities at http://aisdata.ais.dk/. It is not redistributed in this
# repository; this script fetches it and checks the download against a known
# SHA-256.
#
#   tools/fetch_dk_slice.sh out/oresund-2026-04-22.csv          # Øresund only
#   tools/fetch_dk_slice.sh out/oresund-2026-04-22.csv out/     # and the other three, into out/
set -euo pipefail

out=${1:?usage: fetch_dk_slice.sh OERESUND.csv [DIR_FOR_OTHER_SLICES]}
extra=${2:-}
day=2026-04-22
url="http://aisdata.ais.dk/aisdk-${day}.zip"
sha256=31852be42b9488e5893add945d30de7b9af19faca7328822a0f3cd8411ca7cdf

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "downloading $url (about 830 MB)" >&2
curl -fsS --retry 3 -o "$tmp/day.zip" "$url"
echo "$sha256  $tmp/day.zip" | sha256sum -c - >&2

mkdir -p "$(dirname "$out")"
if [ -n "$extra" ]; then mkdir -p "$extra"; fi
# Columns kept: time, class, MMSI, lat, lon, SOG, COG, heading, ship type.
unzip -p "$tmp/day.zip" | awk -F, -v oresund="$out" -v extra="$extra" '
  function row() { return $1 "," $2 "," $3 "," $4 "," $5 "," $8 "," $9 "," $10 "," $14 }
  function in_box(h0, h1, la0, la1, lo0, lo1) { return h >= h0 && h < h1 && $4 >= la0 && $4 <= la1 && $5 >= lo0 && $5 <= lo1 }
  NR > 1 && ($2 == "Class A" || $2 == "Class B") {
    split($1, d, /[\/ :]/); h = d[4] + 0
    if (in_box(12, 14, 55.3, 56.2, 12.3, 13.2)) print row() > oresund
    if (extra == "") next
    if (in_box(16, 18, 55.0, 55.8, 10.6, 11.4)) print row() > (extra "/storebaelt-2026-04-22.csv")
    if (in_box(6, 8, 57.3, 58.0, 10.0, 11.2)) print row() > (extra "/skagen-2026-04-22.csv")
    if (in_box(20, 22, 54.9, 55.4, 14.5, 15.3)) print row() > (extra "/bornholm-2026-04-22.csv")
  }'
echo "wrote $(wc -l < "$out") reports to $out" >&2
if [ -n "$extra" ]; then wc -l "$extra"/*-2026-04-22.csv >&2; fi
