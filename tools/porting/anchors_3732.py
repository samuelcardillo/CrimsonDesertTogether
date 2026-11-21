#!/usr/bin/env python3
"""Anchors for the 3663 -> 3732 port (docs/research/port-3732.md). Read-only on the installed exe (1.0.0.3732).

The 3663 exe is gone (Steam replaces it), so the anchors come from the source as it was for 3663 (run this on a
checkout of the commit before the port, 8264c1f, or the pairing below has nothing to pair):

1. Every byte array of src/ (8 bytes or more) is searched in the WHOLE 3732 code section with its relative operands
   masked (signature_map.relative_mask). 305 of 1049 have exactly one match.
2. The 3663 address of such an array is a hex literal of the same file 0..0x6000 below the match (the code only
   grew). When there is one candidate, that is the pair. The pairs that break a non-decreasing shift are dropped, and
   the rest of the arrays are paired in rounds: the one candidate whose shift lies between the shifts of the pairs
   around it.
3. Every relative operand inside a paired array gives one more anchor: the old bytes say where it pointed in 3663,
   the new bytes where it points in 3732 (a call's target: code; a RIP-relative operand: usually data).

  python tools/porting/anchors_3732.py [--exe PATH] [--out tools/porting/data/anchors_3732.json]

Result on 2026-11-21: 156 anchors (137 code, 19 data); the shift grows from +0x370 near 0x67xxxx to about +0x50xx
near 0x2C7xxxx and is NOT constant between two anchors, so an anchor only says where to look (p3732lib.near/guess).
From that day a copy of each build's exe is kept in backups/exe/, so the next port can compare two exes directly.
"""
from __future__ import annotations

import argparse
import bisect
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(ROOT / "tools" / "scripts"))
from rtti_dump import Pe  # noqa: E402
from signature_map import CS, ImageScanner, relative_mask  # noqa: E402

ARRAY = re.compile(r"\b(?:uint8_t|unsigned\s+char|BYTE)\s+(\w+)\s*\[\s*(?:\w+)?\s*\]\s*=\s*\{([^}]+)\}\s*;([^\n]*)", re.S)
HEX = re.compile(r"0x([0-9A-Fa-f]{5,8})\b")
CODE_END_3663 = 0x5613000


def collect(root=ROOT):
    """(arrays, literals): every byte array of src/ and, per file, every hex literal of 5 to 8 digits."""
    arrays, literals = [], {}
    for path in sorted((root / "src").rglob("*")):
        if path.suffix not in (".h", ".cpp", ".inc"):
            continue
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        rel = path.relative_to(root).as_posix()
        literals[rel] = sorted({int(m[1], 16) for m in HEX.finditer(text)})
        for m in ARRAY.finditer(text):
            tokens = [t.strip() for t in re.sub(r"//[^\n]*", "", m[2]).split(",") if t.strip()]
            if not tokens or not all(re.fullmatch(r"0[xX][0-9a-fA-F]{1,2}|[0-9]{1,3}", t) for t in tokens):
                continue
            arrays.append({"file": rel, "line": text.count("\n", 0, m.start()) + 1, "name": m[1],
                           "data": bytes(int(t, 0) for t in tokens)})
    return arrays, literals


def operands(data, address):
    """[(offset of the relative field, 'call'|'rip', absolute target)] for code at address."""
    out = []
    mask, records, decoded = relative_mask(data, address)
    for insn in CS.disasm(data, address):
        at = insn.address - address
        if at + insn.size > decoded:
            break
        for r in records:
            if at <= r["offset"] < at + insn.size:
                if r["kind"] == "relative_branch":
                    out.append((r["offset"], "call", insn.operands[0].imm & 0xFFFFFFFFFFFFFFFF))
                else:
                    disp = int.from_bytes(data[r["offset"]:r["offset"] + 4], "little", signed=True)
                    out.append((r["offset"], "rip", insn.address + insn.size + disp))
    return out


def window(points, old):
    """(lowest, highest) shift the pairs around a 3663 address allow."""
    keys = [p[0] for p in points]
    i = bisect.bisect_right(keys, old)
    lo = points[i - 1][1] if i > 0 else 0
    hi = points[i][1] if i < len(points) else 0x6000
    return min(lo, hi), max(lo, hi)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=ROOT.parent / "bin64" / "CrimsonDesert.exe")
    ap.add_argument("--out", type=Path, default=Path(__file__).parent / "data" / "anchors_3732.json")
    args = ap.parse_args()
    pe = Pe(args.exe.read_bytes())
    if pe.timestamp != 0x6AFDC0CF:
        raise SystemExit(f"not 1.0.0.3732 (timestamp {pe.timestamp:#x})")
    scan = ImageScanner(pe, cap=6)
    starts = set(scan.starts)
    arrays, literals = collect()
    unique = []
    for a in arrays:
        if len(a["data"]) < 8:
            continue
        mask, records, _ = relative_mask(a["data"], 0)
        hits, _ = scan.scan(a["data"], mask if records else None)
        if len(hits) == 1:
            new = hits[0]["rva"]
            cands = [v for v in literals[a["file"]] if 0 <= new - v <= 0x6000]
            if new in starts:  # a function head: functions are 16-aligned in both builds
                cands = [c for c in cands if (new - c) % 16 == 0]
            unique.append((a, new, cands))
    paired = {i: cands[0] for i, (a, new, cands) in enumerate(unique) if len(cands) == 1}
    # the longest chain of pairs with a non-decreasing shift; the others were a wrong lone literal
    pts = sorted((old, unique[i][1] - old, i) for i, old in paired.items())
    best, prev = [1] * len(pts), [-1] * len(pts)
    for x in range(len(pts)):
        for y in range(x):
            if pts[y][1] <= pts[x][1] and best[y] + 1 > best[x]:
                best[x], prev[x] = best[y] + 1, y
    keep, x = set(), max(range(len(pts)), key=lambda k: best[k]) if pts else -1
    while x >= 0:
        keep.add(pts[x][2])
        x = prev[x]
    paired = {i: old for i, old in paired.items() if i in keep}
    while True:
        points = sorted((old, unique[i][1] - old) for i, old in paired.items())
        added = 0
        for i, (a, new, cands) in enumerate(unique):
            if i in paired:
                continue
            ok = [c for c in cands if window(points, c)[0] - 0x10 <= new - c <= window(points, c)[1] + 0x10]
            if len(ok) == 1:
                paired[i] = ok[0]
                added += 1
        if not added:
            break
    anchors = {}
    for i, old in paired.items():
        a, new, _ = unique[i]
        anchors.setdefault((old, new), {"old": old, "new": new, "kind": "array",
                                        "from": a["file"].split("/")[-1] + ":" + a["name"]})
        off = pe.rva_to_off(new)
        try:
            was, now = operands(a["data"], old), operands(pe.data[off:off + len(a["data"])], new)
        except Exception:
            continue
        if len(was) != len(now):
            continue
        for (o1, k1, t1), (o2, k2, t2) in zip(was, now):
            if o1 == o2 and k1 == k2 and 0 <= t2 - t1 <= 0x9000:
                anchors.setdefault((t1, t2), {"old": t1, "new": t2, "kind": k1, "from": f"{a['name']}+{o1:#x}"})
    out = sorted(anchors.values(), key=lambda r: r["old"])
    for r in out:
        r["shift"] = r["new"] - r["old"]
    left = [(a["file"], a["name"], hex(new), [hex(c) for c in cands])
            for i, (a, new, cands) in enumerate(unique) if i not in paired]
    args.out.write_text(json.dumps({"anchors": out, "unpaired": left}, indent=1), encoding="utf-8", newline="\n")
    code = [r for r in out if r["old"] < CODE_END_3663]
    print(f"arrays in src: {len(arrays)}; with one match in 3732: {len(unique)}; paired with a 3663 address: {len(paired)}")
    print(f"anchors: {len(out)} ({len(code)} code, {len(out) - len(code)} data); unpaired arrays: {len(left)}")
    last = None
    for r in out:
        if r["shift"] != last:
            print(f"  from {r['old']:#09x}: {r['shift']:+#07x}  {r['kind']:5} {r['from'][:70]}")
            last = r["shift"]


if __name__ == "__main__":
    main()
