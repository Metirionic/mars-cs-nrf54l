#!/usr/bin/env python3
"""COBS frame validator for raw captures of the DK's uart20 ranging stream.

Classifies every 0x00-delimited segment:

  * log/banner frame  - tiny payload (device log messages share the wire)
  * ranging frame     - payload close to the modal ranging size
  * TRUNCATED         - ranging frame materially shorter than the norm
                        (bytes dropped inside the frame)
  * MERGED            - materially longer than the norm
                        (a lost delimiter spliced two frames together)
  * malformed         - does not COBS-decode at all

The leading and trailing segments are reported separately: the harness starts
and stops the reader mid-stream, so those two are partial by construction.

Usage: cobs_check.py <capture.bin> [device_procedures] [bytes_per_procedure]
"""
import collections
import sys

TINY = 200          # <= this is a log/banner frame
TOLERANCE = 0.005   # 0.5% deviation from the modal ranging size


def cobs_decode(data: bytes):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        code = data[i]
        i += 1
        if code == 0:
            return None
        take = code - 1
        if i + take > n:
            return None
        out += data[i:i + take]
        i += take
        if code < 0xFF and i < n:
            out.append(0)
    return bytes(out)


def main() -> int:
    raw = open(sys.argv[1], "rb").read()
    segments = raw.split(b"\x00")
    leading, trailing = segments[0], segments[-1]
    frames = [s for s in segments[1:-1] if s]

    decoded = []
    malformed = 0
    for f in frames:
        d = cobs_decode(f)
        if d is None:
            malformed += 1
        else:
            decoded.append(len(d))

    tiny = [n for n in decoded if n <= TINY]
    ranging = [n for n in decoded if n > TINY]

    print(f"file             : {sys.argv[1]} ({len(raw)} bytes)")
    print(f"segments         : {len(frames)} frames "
          f"({len(ranging)} ranging, {len(tiny)} log, {malformed} malformed)")
    print(f"leading partial  : {len(leading)} B    trailing partial: {len(trailing)} B")
    if not ranging:
        print("VERDICT          : no ranging frames captured")
        return 1

    modal = collections.Counter(ranging).most_common(1)[0][0]
    lo, hi = modal * (1 - TOLERANCE), modal * (1 + TOLERANCE)
    truncated = sorted(n for n in ranging if n < lo)
    merged = sorted(n for n in ranging if n > hi)
    clean = len(ranging) - len(truncated) - len(merged)

    print(f"ranging frame    : modal {modal} B  (clean {clean})")
    if truncated:
        print(f"TRUNCATED        : {len(truncated)} -> "
              f"{' '.join(str(n) for n in truncated[:12])}")
    if merged:
        print(f"MERGED           : {len(merged)} -> "
              f"{' '.join(str(n) for n in merged[:12])}")

    corrupt = len(truncated) + len(merged) + malformed
    print(f"corrupt frames   : {corrupt}")

    if len(sys.argv) > 3:
        procs = int(sys.argv[2])
        per_proc = int(sys.argv[3])
        sent = procs * per_proc
        lost = sent - len(raw)
        print(f"device sent      : {procs} procs x {per_proc} B = {sent} B")
        print(f"captured         : {len(raw)} B")
        print(f"LOSS             : {lost} B ({100.0 * lost / sent:.2f}%)"
              if sent else "")
    return 1 if corrupt else 0


if __name__ == "__main__":
    sys.exit(main())
