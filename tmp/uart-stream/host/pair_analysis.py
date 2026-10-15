#!/usr/bin/env python3
"""pair_analysis.py -- independent verification analysis for the #190 dual captures.

Not part of the capture/check loop: this re-derives the cross-claims
(pairing, grid, bracketing read-gap, phase-marker alternation) straight from
the saved captures + .tsv sidecars, so the numbers do not depend on the
previous session's ad-hoc analysis.

Usage:
  python3 pair_analysis.py pairing A.json B.json
  python3 pair_analysis.py grid A.json
  python3 pair_analysis.py bracket A.json A.bin A.tsv
  python3 pair_analysis.py phases A.bin

pairing: nearest-neighbour event matching between two runs' check-JSON
reports, with a tolerance scan (is "16 of 20" offset-fishing?) and the
independence prediction.

grid: inter-event gaps and a t mod P fit per run (P free, drift free).

bracket: for each loss event, the read-gap in the .tsv that *contains* the
divergence byte (the previous session's retracted claim came from measuring a
window instead of the containing read).

phases: decode the status-frame phase marker over the whole capture and verify
it alternates every 64 bursts (192 frames) as CONFIG_STREAM_ALT_PERIOD says.
"""

import json
import sys
import bisect
from collections import Counter

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import stream_check as sc  # audited: parsing + regeneration primitives only


# ---------------------------------------------------------------- pairing --

def pairing(path_a, path_b, tol=0.5):
    a = [l["at_s"] for l in json.load(open(path_a))["anomalies"] if l["at_s"] is not None]
    b = [l["at_s"] for l in json.load(open(path_b))["anomalies"] if l["at_s"] is not None]
    n_a, n_b = len(a), len(b)
    dur = 600.0  # both 600 s runs; for 300 s runs the prediction scales the same way
    lam_b = n_b / dur
    expected_chance = n_a * (2 * tol) * lam_b
    print(f"A: {n_a} events, B: {n_b} events; independence predicts "
          f"{expected_chance:.1f} matches within ±{tol}s")

    def matches(delta):
        """Greedy 1:1 nearest-neighbour matching after shifting B by delta."""
        bs = sorted(b)
        used = set()
        out = []
        for t in sorted(a):
            best, bd = None, tol
            for j, u in enumerate(bs):
                if j in used:
                    continue
                d = abs(u + delta - t)
                if d <= bd:
                    best, bd = j, d
            if best is not None:
                used.add(best)
                out.append((t, bs[best] + delta, bd))
        return out

    m0 = matches(0.0)
    deltas = sorted(d for _, _, d in m0)
    med = deltas[len(deltas) // 2] if deltas else None
    print(f"offset +0.00 s: {len(m0)}/{n_a} matched, deltas(s)="
          f"{[round(d, 3) for d in deltas]}")
    if med is not None:
        print(f"  median |delta| = {med*1000:.0f} ms")

    # tolerance scan: how many matches survive at 0.1/0.25/0.5/1.0 s?
    for t2 in (0.1, 0.25, 0.5, 1.0):
        print(f"  ±{t2:4.2f}s -> {matches_at(a, b, 0.0, t2)} matched")

    # offset-fishing check: can any shift over ±2 s beat the +0 alignment?
    counts = [(matches_at(a, b, d, tol), d) for d in [x / 100 for x in range(-200, 201)]]
    best = max(c for c, _ in counts)
    print(f"best match count over ±2 s of shift: {best} (vs {len(m0)} at +0.00) -- "
          f"the zero-shift pairing {'TIES the maximum' if best == len(m0) else 'is beaten elsewhere'}")


def matches_at(a, b, delta, tol):
    bs = sorted(b)
    used = set()
    n = 0
    for t in sorted(a):
        best, bd = None, tol
        for j, u in enumerate(bs):
            if j in used:
                continue
            d = abs(u + delta - t)
            if d <= bd:
                best, bd = j, d
        if best is not None:
            used.add(best)
            n += 1
    return n


def start_mono(tsv_path):
    """The absolute CLOCK_MONOTONIC (s) recorded at capture start, or None."""
    with open(tsv_path) as f:
        for line in f:
            if line.startswith("# start_mono "):
                return int(line.split()[2]) / 1e9
            if not line.startswith("#"):
                break
    return None


def abs_pairing(json_a, tsv_a, json_b, tsv_b, tol=0.5):
    """Pairing with exact cross-capture clock alignment via start_mono."""
    ta = start_mono(tsv_a)
    tb = start_mono(tsv_b)
    if ta is None or tb is None:
        print("no # start_mono header in one of the .tsv files")
        return
    a_abs = [t + ta for t in
             (l["at_s"] for l in json.load(open(json_a))["anomalies"])
             if t is not None]
    b_abs = [t + tb for t in
             (l["at_s"] for l in json.load(open(json_b))["anomalies"])
             if t is not None]
    print(f"A start_mono={ta:.6f}, B start_mono={tb:.6f}, "
          f"launch skew {(tb-ta)*1000:+.1f} ms")
    print(f"A: {len(a_abs)} events, B: {len(b_abs)} events (absolute times)")
    used = set()
    deltas = []
    for t in sorted(a_abs):
        best, bd = None, tol
        for j, u in enumerate(sorted(b_abs)):
            if j in used:
                continue
            d = abs(u - t)
            if d <= bd:
                best, bd = j, d
        if best is not None:
            used.add(best)
            deltas.append(sorted(b_abs)[best] - t)
    print(f"matched {len(deltas)} of {len(a_abs)} within ±{tol}s; "
          f"cross-board deltas (ms): {[round(d*1000,1) for d in sorted(deltas, key=abs)]}")
    if deltas:
        s = sorted(abs(d) for d in deltas)
        print(f"median |delta| = {s[len(s)//2]*1000:.1f} ms")


# -------------------------------------------------------------------- grid --

def grid(path):
    ts = sorted(l["at_s"] for l in json.load(open(path))["anomalies"]
                if l["at_s"] is not None)
    print(f"{len(ts)} events: {[round(t, 2) for t in ts]}")
    if len(ts) < 2:
        return
    gaps = [round(b - a, 3) for a, b in zip(ts, ts[1:])]
    print(f"gaps: {gaps}")
    # fit period P and phase over the longest near-6s stretch: try P in 5.5..6.5
    best = None
    for P100 in range(550, 651):
        P = P100 / 100
        # least-squares phase: t_i mod P should be constant for on-grid events;
        # score = circular variance of (t mod P)
        ph = [((t % P) / P) * 2 * 3.14159265 for t in ts]
        sx = sum(__import__("math").cos(p) for p in ph)
        sy = sum(__import__("math").sin(p) for p in ph)
        r = ((sx * sx + sy * sy) ** 0.5) / len(ts)
        if best is None or r > best[0]:
            best = (r, P)
    r, P = best
    print(f"best period {P:.2f}s, concentration r={r:.2f} (1 = every event on one phase)")
    resid = [round(t - P * round(t / P), 3) for t in ts]
    print(f"t mod {P:.2f} residuals vs nearest grid point: {resid}")


# ---------------------------------------------------------------- bracket --

def load_tsv(path):
    offs, times = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            offs.append(int(parts[0]))
            times.append(int(parts[1]))
    return offs, times


def bracket(json_path, bin_path, tsv_path):
    rep = json.load(open(json_path))
    offs, times = load_tsv(tsv_path)
    n = len(offs)
    print(f"{n} reads over {(times[-1]-times[0])/1e9:.0f}s = "
          f"{n/((times[-1]-times[0])/1e9):.0f} reads/s")
    allgaps = [(times[i] - times[i-1]) / 1e6 for i in range(1, n)]
    allgaps_sorted = sorted(allgaps)
    med = allgaps_sorted[len(allgaps)//2]
    p95 = allgaps_sorted[int(len(allgaps)*0.95)]
    print(f"read-gap distribution: median {med:.2f} ms, p95 {p95:.2f} ms, "
          f"max {allgaps_sorted[-1]:.1f} ms")

    for l in rep["anomalies"]:
        if l["bytes_lost"] < 0:
            print(f"  [skip {l['at_s']:.2f}s] negative loss (reboot artifact)")
            continue
        # divergence byte = forensic segment offset + first difference in frame
        div = None
        for f in l["forensics"]:
            if f["kind"] == "frame_drop":
                div = f["at_offset"] + f["first_difference_at"]
                break
        target = div if div is not None else l["at_offset"]
        i = bisect.bisect_left(offs, target)
        i = min(i, n - 1)
        gap = (times[i] - times[i-1]) / 1e6 if i > 0 else float("nan")
        nxt = (times[i+1] - times[i]) / 1e6 if i + 1 < n else float("nan")
        print(f"  loss @ {l['at_s']:8.2f}s: divergence byte {target:,} arrived in read "
              f"#{i}; bracketing read-gaps prev {gap:.2f} ms / next {nxt:.2f} ms "
              f"({'divergence byte forensics' if div is not None else 'next-valid-frame offset'})")


# ----------------------------------------------------------------- phases --

def phases(bin_path):
    raw = open(bin_path, "rb").read()
    marks = []
    for off, seg in sc.split_segments(raw):
        fr = sc.parse_segment(seg)
        if fr is not None and (fr.flags & sc.FLAG_STATUS) and fr.payload_n == 1:
            marks.append((fr.seq, fr.decoded[sc.HDR_LEN]))
    print(f"{len(marks)} status frames decoded")
    # phase should switch every CONFIG_STREAM_ALT_PERIOD*3 frames (64 bursts * 3)
    switches = []
    for (s1, m1), (s2, m2) in zip(marks, marks[1:]):
        if m1 != m2:
            switches.append(s2)
    print(f"marker values: {Counter(m for _, m in marks)}")
    print(f"{len(switches)} switches at seq {switches}")
    if switches:
        d = [round(b - a) for a, b in zip(switches, switches[1:])]
        print(f"switch spacing (frames): {d} -- expect ~192 = 64 bursts x 3")
    # any status frame NOT 1 or 2?
    bad = [(s, m) for s, m in marks if m not in (1, 2)]
    print(f"status frames with marker outside (1,2): {bad[:5]} ({len(bad)} total)")


def main():
    cmd = sys.argv[1]
    if cmd == "pairing":
        pairing(sys.argv[2], sys.argv[3])
    elif cmd == "grid":
        grid(sys.argv[2])
    elif cmd == "bracket":
        bracket(sys.argv[2], sys.argv[3], sys.argv[4])
    elif cmd == "abs-pairing":
        abs_pairing(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif cmd == "phases":
        phases(sys.argv[2])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())