#!/usr/bin/env bash
# Runs the collision-risk evaluation reported in docs/collision-risk-evaluation.md
# on the Danish slices written by tools/fetch_dk_slice.sh.
#
#   tools/evaluate_collision_risk.sh build/mt-risk data/
#
# For each slice: CPA/TCPA accuracy against the recorded future, and the
# behaviour check (did the give-way vessel open the passing distance more than
# the stand-on vessel?) for all vessels, then for cargo ships, tankers,
# passenger ships and high-speed craft only (where Rule 18 does not change the
# roles), at 0.5 nm and 0.25 nm.
#
# Exits non-zero if the headline results regress:
#   - Oresund, Kalman predictor: median CPA distance error at most 60 m at
#     0-2 min and at most 200 m at 2-5 min
#   - Oresund, all vessels: in overtaking, the give-way vessel opened the
#     distance more in at least 80% of developing encounters
set -euo pipefail

bin=${1:?usage: evaluate_collision_risk.sh MT_RISK DATA_DIR}
dir=${2:?usage: evaluate_collision_risk.sh MT_RISK DATA_DIR}
fail=0
check() {  # check DESCRIPTION CONDITION
    if awk "BEGIN { exit !($2) }"; then echo "ok:   $1" >&2; else echo "FAIL: $1" >&2; fail=1; fi
}
section() {  # section TITLE FILE
    echo "### $1"
    echo
    echo '```'
    sed -n '1,3p' "$2"
    echo '```'
    echo
    sed -n '/^| Time to CPA/,/^$/p' "$2"
    sed -n '/^encounters:/p' "$2"
    echo
    sed -n '/^| Encounter/,/^$/p' "$2"
    echo
}

for slice in oresund storebaelt skagen bornholm; do
    csv="$dir/$slice-2026-04-22.csv"
    all=$(mktemp); all_close=$(mktemp); com=$(mktemp); close=$(mktemp)
    "$bin" --dk-csv "$csv" > "$all"
    "$bin" --dk-csv "$csv" --max-dcpa-nm 0.25 > "$all_close"
    "$bin" --dk-csv "$csv" --commercial-only > "$com"
    "$bin" --dk-csv "$csv" --commercial-only --max-dcpa-nm 0.25 > "$close"
    echo "## $slice"
    echo
    section "All vessels, CPA within 0.5 nm" "$all"
    section "All vessels, CPA within 0.25 nm" "$all_close"
    section "Cargo, tanker, passenger and high-speed craft only, CPA within 0.5 nm" "$com"
    section "Cargo, tanker, passenger and high-speed craft only, CPA within 0.25 nm" "$close"

    if [ "$slice" = oresund ]; then
        m1=$(grep -E '^\| 0-2 min' "$all" | awk -F'|' '{split($5, a, " "); print a[1]}')
        m2=$(grep -E '^\| 2-5 min' "$all" | awk -F'|' '{split($5, a, " "); print a[1]}')
        check "oresund: Kalman median CPA error at 0-2 min ${m1} m (<= 60)" "$m1 <= 60"
        check "oresund: Kalman median CPA error at 2-5 min ${m2} m (<= 200)" "$m2 <= 200"
        ot=$(grep -E '^\| overtaking' "$all" | awk -F'|' '{print $8}' | sed -E 's/.*\(([0-9.]+)%\).*/\1/')
        check "oresund: overtaking, give-way opened the distance more in ${ot}% (>= 80%)" "$ot >= 80"
    fi
    rm -f "$all" "$all_close" "$com" "$close"
done

exit $fail
