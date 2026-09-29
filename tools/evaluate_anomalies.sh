#!/usr/bin/env bash
# Runs the anomaly evaluation reported in docs/anomaly-evaluation.md on the
# four Danish slices written by tools/fetch_dk_slice.sh.
#
#   tools/evaluate_anomalies.sh build/mt-anomaly data/
#
# For each slice: the flags raised on the data as recorded, then 100 planted
# cases of each kind (5 per seed, seeds 1-20). Then, on all slices together,
# smaller planted cases to show where detection stops.
#
# Exits non-zero if the headline results regress:
#   - clear-cut planted cases: at least 95% found, per kind and slice (gaps on
#     the Great Belt slice, which contains a receiver outage: at least 75%)
#   - at most 10 flags on any slice as recorded
#   - at most 5 extra flags caused by planting, per slice
set -euo pipefail

bin=${1:?usage: evaluate_anomalies.sh MT_ANOMALY DATA_DIR}
dir=${2:?usage: evaluate_anomalies.sh MT_ANOMALY DATA_DIR}
plant="--inject 5 --seeds 20"
fail=0

recall() {  # recall KIND FILE -> percentage without the % sign
    grep -E "^\| $1 \| [0-9]+ \| [0-9]+ \| [0-9.]+%" "$2" | awk -F'|' '{gsub(/[ %]/, "", $5); print $5}'
}
check() {  # check DESCRIPTION CONDITION
    if awk "BEGIN { exit !($2) }"; then echo "ok:   $1" >&2; else echo "FAIL: $1" >&2; fail=1; fi
}

for slice in oresund storebaelt skagen bornholm; do
    csv="$dir/$slice-2026-04-22.csv"
    out=$(mktemp)
    "$bin" --dk-csv "$csv" $plant --list > "$out"
    echo "## $slice"
    echo
    echo '```'
    sed -n '1,2p' "$out"
    grep -E '^(second|silences|reports from)' "$out"
    echo '```'
    echo
    echo "Flags on the data as recorded:"
    echo
    sed -n '/^| Flag/,/^$/p' "$out"
    sed -n '/^planted:/,/^$/p' "$out"
    sed -n '/^| Planted anomaly/,/^$/p' "$out"
    grep '^flags caused' "$out"
    echo
    if grep -q '^kind,mmsi' "$out" && [ "$(sed -n '/^kind,mmsi/,$p' "$out" | wc -l)" -gt 1 ]; then
        echo "Each flag on the data as recorded:"
        echo
        echo '```'
        sed -n '/^kind,mmsi/,$p' "$out"
        echo '```'
        echo
    fi

    flags=$(sed -n '/^| Flag/,/^$/p' "$out" | awk -F'|' 'NR > 2 && NF > 3 {s += $3} END {print s + 0}')
    extra=$(sed -nE 's/^flags caused by planting that match no planted case: ([0-9]+)/\1/p' "$out")
    check "$slice: $flags flags on the data as recorded (<= 10)" "$flags <= 10"
    check "$slice: $extra extra flags caused by planting (<= 5)" "$extra <= 5"
    for kind in impossible_speed position_jump gap identity_conflict; do
        r=$(recall "$kind" "$out")
        min=95
        if [ "$slice" = storebaelt ] && [ "$kind" = gap ]; then min=75; fi
        check "$slice: $kind recall $r% (>= $min%)" "$r >= $min"
    done
    rm -f "$out"
done

# Sensitivity: smaller planted cases, all four slices pooled.
sweep() {  # sweep KIND LABEL ARGS...
    local kind=$1 label=$2; shift 2
    local planted=0 found=0
    for slice in oresund storebaelt skagen bornholm; do
        read -r p f < <("$bin" --dk-csv "$dir/$slice-2026-04-22.csv" $plant "$@" |
            grep -E "^\| $kind \| [0-9]+ \| [0-9]+ \| [0-9.]+%" | awk -F'|' '{print $3, $4}')
        planted=$((planted + p)); found=$((found + f))
    done
    awk -v l="$label" -v p="$planted" -v f="$found" 'BEGIN { printf "| %s | %d | %d | %.1f%% |\n", l, p, f, 100 * f / p }'
}
echo "## Sensitivity (all four slices)"
echo
echo "| Planted case | Planted | Detected | Recall |"
echo "|---|---:|---:|---:|"
sweep position_jump "Jump 0.5-1 km" --jump-km 0.5,1
sweep position_jump "Jump 1-2 km" --jump-km 1,2
sweep position_jump "Jump 2-3 km" --jump-km 2,3
sweep position_jump "Jump 3-30 km" --jump-km 3,30
sweep gap "Gap 5-10 min (below the 10 min rule)" --gap-min 5,10
sweep gap "Gap 10-15 min" --gap-min 10,15
sweep gap "Gap 15-40 min" --gap-min 15,40
sweep identity_conflict "Second transmitter 1-2 km away" --conflict-km 1,2
sweep identity_conflict "Second transmitter 2-5 km away" --conflict-km 2,5
sweep identity_conflict "Second transmitter 5+ km away" --conflict-km 5,1e9
sweep impossible_speed "Speed 50-55 kn" --speed-kn 50.1,55

exit $fail
