#!/usr/bin/env bash
# Runs the tracker evaluation reported in docs/tracker-evaluation.md.
# Parameters are tuned on the first hour (12:00-13:00 UTC) and every number
# in the report comes from the held-out second hour (13:00-14:00 UTC).
#
#   tools/evaluate_tracker.sh build/mt-track out/oresund-2026-04-22.csv
#
# Exits non-zero if the headline results regress:
#   - with noisy input, the Kalman filter must beat dead reckoning at 10-30 s
#   - the gate must reject at least 80% of injected outliers
set -euo pipefail

bin=${1:?usage: evaluate_tracker.sh MT_TRACK CSV}
csv=${2:?usage: evaluate_tracker.sh MT_TRACK CSV}
train="--eval-to 1776862800"                        # 12:00-13:00 UTC
test="--eval-from 1776862800 --eval-to 1776866400"  # 13:00-14:00 UTC
noisy="--noise 15 --drop-velocity 0.3"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "## Tuning (first hour)"
echo
echo "### Clean input"
"$bin" --dk-csv "$csv" $train --sigma-a 0.02,0.05,0.1 --pos-sigma 3,5,10 --sigma-vel 0.1,0.3 | sed 1,2d
echo
echo "### Noisy input ($noisy --outliers 0.01)"
"$bin" --dk-csv "$csv" $train $noisy --outliers 0.01 --sigma-a 0.02,0.05,0.1 --pos-sigma 15,25 --sigma-vel 0.1,0.3 | sed 1,2d

run() {
    local name=$1; shift
    echo
    echo "## Held-out hour: $name"
    echo
    echo '```'
    "$bin" --dk-csv "$csv" $test "$@" | tee "$work/$name.txt" | sed -n '1,5p'
    echo '```'
    sed -n '6,$p' "$work/$name.txt"
}

run clean --pos-sigma 5
run noisy --pos-sigma 25 $noisy
run outliers --pos-sigma 5 --outliers 0.01
run combined --pos-sigma 25 $noisy --outliers 0.01

# Regression checks on the held-out results.
row=$(grep -A10 '^Moving' "$work/noisy.txt" | grep '^| 10-30 s')
dr=$(echo "$row" | awk -F'|' '{split($5, a, " "); print a[1]}')
kf=$(echo "$row" | awk -F'|' '{split($6, a, " "); print a[1]}')
pct=$(grep '^gate' "$work/outliers.txt" | sed -E 's/.*outliers \(([0-9.]+)%\).*/\1/')
echo
echo "check: noisy 10-30 s median, Kalman ${kf} m vs dead reckoning ${dr} m; outliers rejected ${pct}%" >&2
awk -v kf="$kf" -v dr="$dr" -v pct="$pct" 'BEGIN { exit !(kf < dr && pct >= 80) }'
