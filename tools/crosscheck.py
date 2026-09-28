#!/usr/bin/env python3
"""Cross-check mt-decode against pyais, an independent open-source AIS decoder.

The capture is read line by line. Single-sentence messages go straight to
pyais; multi-sentence messages are grouped by (fragment count, sequence id,
channel) and handed to pyais as a group. Each decoded message is keyed by the
line that completed it, which mt-decode also reports, and the two results are
compared field by field. Exit status is non-zero on any mismatch.

    ./build/mt-decode < capture.nmea > mt.jsonl
    python3 tools/crosscheck.py capture.nmea mt.jsonl
"""

import json
import math
import sys
from collections import Counter

import pyais
from pyais.constants import ShipType

FULL_TYPES = {1, 2, 3, 5, 18, 19, 24}


def sentence_of(line):
    """Returns the NMEA sentence without any tag block, or None."""
    line = line.strip()
    if line.startswith("\\"):
        end = line.find("\\", 1)
        if end < 0:
            return None
        line = line[end + 1:]
    return line if line.startswith("!") else None


def reference_messages(path):
    """Maps 1-based completing line number -> pyais dict (or None if pyais fails)."""
    out = {}
    pending = {}
    with open(path, encoding="ascii", errors="replace") as f:
        for n, raw in enumerate(f, start=1):
            s = sentence_of(raw)
            if s is None:
                continue
            fields = s.split(",")
            if len(fields) != 7:
                continue
            try:
                count, number = int(fields[1]), int(fields[2])
            except ValueError:
                continue
            if count == 1:
                parts = [s]
            else:
                key = (count, fields[3], fields[4])
                if number == 1:
                    pending[key] = [s]
                    continue
                group = pending.get(key)
                if group is None or len(group) + 1 != number:
                    pending.pop(key, None)
                    continue
                group.append(s)
                if number < count:
                    continue
                parts = pending.pop(key)
            try:
                out[n] = pyais.decode(*[p.encode() for p in parts]).asdict()
            except Exception:  # pyais rejects some malformed messages
                out[n] = None
    return out


def bad_fill_bits(line):
    s = sentence_of(line)
    if s is None or "*" not in s:
        return False
    fill = s.rsplit("*", 1)[0].rsplit(",", 1)[-1]
    return not (fill.isdigit() and 0 <= int(fill) <= 5)


def close(a, b, tol=1e-6):
    if a is None or b is None:
        return a is None and b is None
    return math.isclose(float(a), float(b), abs_tol=tol)


def norm_text(s):
    return (s or "").rstrip("@ ").split("@")[0].rstrip()


def na(value, sentinels):
    return None if value is None or value in sentinels else value


def compare(mine, ref):
    """Returns a list of mismatching field names."""
    bad = []
    t = mine["type"]
    if mine["mmsi"] != ref["mmsi"]:
        bad.append("mmsi")
    if t in (1, 2, 3, 18, 19):
        lat = ref.get("lat")
        lon = ref.get("lon")
        if lat is not None and (abs(lat) > 90 or abs(lon) > 180):
            lat = lon = None
        if not close(mine["lat"], lat) or not close(mine["lon"], lon):
            bad.append("position")
        if not close(mine["sog"], na(ref.get("speed"), {102.3})):
            bad.append("sog")
        cog = ref.get("course")
        if not close(mine["cog"], None if cog is None or cog >= 360 else cog):
            bad.append("cog")
        hdg = ref.get("heading")
        if mine["heading"] != (None if hdg is None or hdg > 359 else hdg):
            bad.append("heading")
    if t == 5:
        for field, key in (("name", "shipname"), ("callsign", "callsign"), ("destination", "destination")):
            if mine[field] != norm_text(ref.get(key)):
                bad.append(field)
        # pyais folds reserved and sub-coded ship types into enum buckets
        # (97 -> 95, 1-19 -> 0); mt-decode keeps the raw value, so fold it the same way.
        if int(ShipType(mine["ship_type"])) != int(ref.get("ship_type") or 0):
            bad.append("ship_type")
        if mine["length_m"] != (ref.get("to_bow") or 0) + (ref.get("to_stern") or 0):
            bad.append("length")
    if t == 24:
        if mine["part"] == "A" and mine["name"] != norm_text(ref.get("shipname")):
            bad.append("name")
        if mine["part"] == "B" and mine["callsign"] != norm_text(ref.get("callsign")):
            bad.append("callsign")
    return bad


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    ref = reference_messages(sys.argv[1])
    with open(sys.argv[2]) as f:
        mine = {m["line"]: m for m in map(json.loads, f)}

    ref_ok = {n: r for n, r in ref.items() if r is not None}
    print(f"pyais messages:     {len(ref_ok)} ({len(ref) - len(ref_ok)} rejected by pyais)")
    print(f"mt-decode messages: {len(mine)}")

    only_mine = sorted(set(mine) - set(ref_ok))
    only_ref = sorted(set(ref_ok) - set(mine))
    mismatches = Counter()
    examples = {}
    compared = 0
    for n in sorted(set(mine) & set(ref_ok)):
        m, r = mine[n], ref_ok[n]
        if m["type"] != r["msg_type"]:
            mismatches[(m["type"], "type")] += 1
            continue
        if m["type"] not in FULL_TYPES:
            if m["mmsi"] != r["mmsi"]:
                mismatches[(m["type"], "mmsi")] += 1
            continue
        compared += 1
        for field in compare(m, r):
            mismatches[(m["type"], field)] += 1
            examples.setdefault((m["type"], field), n)

    print(f"compared field by field (types 1-3, 5, 18, 19, 24): {compared}")
    print(f"decoded only by mt-decode: {len(only_mine)} (lines {only_mine[:10]})")
    print(f"decoded only by pyais:     {len(only_ref)} (lines {only_ref[:10]})")
    for (t, field), count in sorted(mismatches.items()):
        print(f"MISMATCH type {t} {field}: {count} (first at line {examples.get((t, field))})")
    # Known, intended difference: mt-decode rejects sentences whose fill-bit
    # field is outside 0-5 (IEC 61162-1); pyais accepts them.
    lines = open(sys.argv[1], encoding="ascii", errors="replace").read().split("\n")
    unexplained = [n for n in only_ref if not bad_fill_bits(lines[n - 1])]
    print(f"  of which rejected by mt-decode for fill bits outside 0-5: {len(only_ref) - len(unexplained)}")
    if mismatches or unexplained:
        if unexplained:
            print(f"UNEXPLAINED pyais-only lines: {unexplained[:20]}")
        return 1
    print("all compared fields match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
