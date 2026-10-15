#!/usr/bin/env python3
"""correlate_activity.py -- do host-daemon CPU bumps line up with loss events?

Takes the dualrun outputs (A.json/B.json + .tsv start_mono) and an
activity_probe.csv, and asks two questions per watched daemon:

  1. Periodicity: what is the daemon's bump spacing? (A daemon polling at a
     fixed cadence shows a spike in the spacing histogram.)
  2. Correlation: is the daemon more likely to bump within +-W ms of a loss
     event than the base rate predicts? Reports bumps-in-window vs expected,
     with the control daemons giving the false-positive scale.

Usage: correlate_activity.py <rundir> <activity.csv> [window_ms]
"""

import json
import sys
from collections import defaultdict


def load_events(rundir):
    evs = []
    for b in "AB":
        rep = json.load(open(f"{rundir}/{b}.json"))
        with open(f"{rundir}/{b}.bin.tsv") as f:
            start = None
            for line in f:
                if line.startswith("# start_mono "):
                    start = int(line.split()[2]) / 1e9
                    break
        if start is None:
            sys.exit(f"no start_mono in {rundir}/{b}.bin.tsv")
        for l in rep["anomalies"]:
            if l["at_s"] is not None:
                evs.append((start + l["at_s"], b, l["bytes_lost"]))
    return sorted(evs), start


def load_bumps(path):
    bumps = defaultdict(list)  # comm -> [mono_s]
    with open(path) as f:
        next(f)
        for line in f:
            mono_ns, comm, pid, delta, cum = line.strip().split(",")
            bumps[comm].append(int(mono_ns) / 1e9)
    return {c: sorted(v) for c, v in bumps.items()}


def main():
    rundir, path = sys.argv[1], sys.argv[2]
    W = float(sys.argv[3]) / 1000.0 if len(sys.argv) > 3 else 0.25

    events, start = load_events(rundir)
    t_end = max(e[0] for e in events) if events else start
    bumps = load_bumps(path)
    dur = 600.0
    print(f"{len(events)} loss events; {sum(len(v) for v in bumps.values()):,} "
          f"daemon bumps total")

    # global window fraction: what fraction of the run lies within W of an event?
    # (events ~40 -> 40*2W/600)
    win_fraction = min(1.0, len(events) * 2 * W / dur)
    print(f"window ±{W*1000:.0f} ms covers {win_fraction*100:.1f}% of the run\n")

    print(f"{'daemon':<28} {'bumps':>6} {'spacing':>10}  in-win  exp   excess")
    rows = []
    for comm, ts in bumps.items():
        n = len(ts)
        spacings = [b - a for a, b in zip(ts, ts[1:])]
        spacings = [s for s in spacings if s < 30]
        med_sp = sorted(spacings)[len(spacings)//2] if spacings else 0
        inwin = sum(1 for e, *_ in events for t in ts
                    if abs(t - e) <= W)
        # each event's window can hold multiple bumps; expected = bumps*frac
        exp = n * win_fraction
        excess = inwin / exp if exp else 0
        rows.append((comm, n, med_sp, inwin, exp, excess))
    for comm, n, sp, inwin, exp, excess in sorted(rows, key=lambda r: -r[5]):
        print(f"{comm:<28} {n:>6} {sp:>9.2f}s  {inwin:>5}  {exp:5.1f}  {excess:6.1f}x")

    print("\nnearest bump per event (<=1 s):")
    for e, board, size in events:
        near = []
        for comm, ts in bumps.items():
            best = min((abs(t - e), t) for t in ts)
            if best[0] <= 1.0:
                near.append((best[0], comm))
        near.sort()
        print(f"  t={e:.3f} {board} {size:>4}B : "
              + ", ".join(f"{c}@{d*1000:.0f}ms" for d, c in near[:4]))


if __name__ == "__main__":
    main()