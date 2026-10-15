#!/usr/bin/env python3
"""activity_probe.py -- root-free host-daemon CPU activity sampler.

Samples /proc/<pid>/schedstat (cumulative on-CPU ns, world-readable) for a set
of candidate daemons at a fixed cadence, logging (monotonic_ns, pid, comm,
delta_ns). Run it alongside a stream_check capture; afterwards, correlate the
deltas with loss-event times. A daemon that only wakes when it has work will
show isolated bumps; a daemon bumping at every loss event is a trigger
suspect. Includes a control daemon (atd/cron/cupsd, whichever exists and is
idle) to estimate the false-positive bump rate.

Usage: activity_probe.py <out.csv> <duration_s> [cadence_ms]
"""

import os
import sys
import time

CANDIDATES = [
    "udisksd", "ModemManager", "systemd-udevd", "fwupd", "fwupd-refresh",
    "upowerd", "gvfs-udisks2-volume-monitor", "systemd-journald",
    "gnome-shell", "NetworkManager", "bluetoothd", "chronyd", "systemd-timesyncd",
]
CONTROL = ["atd", "cron", "cupsd", "rsyslogd", "accounts-daemon"]


def find_pids(names):
    out = {}
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/comm") as f:
                comm = f.read().strip()
        except OSError:
            continue
        if comm in names and comm not in out:
            out[comm] = int(pid)
    return out


def read_schedstat(pid):
    try:
        with open(f"/proc/{pid}/schedstat") as f:
            return int(f.read().split()[0])
    except OSError:
        return None


def main():
    out_path, dur = sys.argv[1], float(sys.argv[2])
    cadence = float(sys.argv[3]) / 1000.0 if len(sys.argv) > 3 else 0.025

    targets = find_pids(CANDIDATES)
    ctrl = find_pids(CONTROL)
    targets.update(ctrl)
    if not targets:
        print("activity_probe: no candidate daemons found", file=sys.stderr)
        return 2
    print(f"activity_probe: watching {sorted(targets)} "
          f"(control: {sorted(set(targets) & set(CONTROL))})")

    state = {c: read_schedstat(p) for c, p in targets.items()}
    t0 = time.monotonic_ns()
    deadline = t0 + int(dur * 1e9)
    n = 0
    with open(out_path, "w") as f:
        f.write("mono_ns,comm,pid,delta_ns,cumulative_ns\n")
        while True:
            now = time.monotonic_ns()
            if now >= deadline:
                break
            for comm, pid in targets.items():
                cur = read_schedstat(pid)
                if cur is None:
                    continue
                prev = state[comm]
                if cur != prev:
                    f.write(f"{now},{comm},{pid},{cur - prev},{cur}\n")
                    state[comm] = cur
            n += 1
            # sleep to the next cadence boundary
            target = t0 + int(n * cadence * 1e9)
            sleep_ns = target - time.monotonic_ns()
            if sleep_ns > 0:
                time.sleep(sleep_ns / 1e9)
    print(f"activity_probe: {n} rounds over {dur:.0f}s -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())