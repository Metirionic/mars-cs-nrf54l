#!/usr/bin/env python3
"""multi_run.py -- enumerate every deleted run inside each damaged frame.

stream_check's forensics locate only the FIRST deletion in a frame (a single
realignment probe). During verification, two events turned out to carry two
deleted runs in one frame (19+74 B and 25+101 B), which the checker's forensic
under-counts while its sent_total accounting still catches the total. This
tool walks each unparsed segment against its regenerated frame and reports
every divergence, with the same seam-skew realignment the checker uses.

Usage: python3 multi_run.py [capture.bin ...]   (defaults: the six dual runs)
"""

import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import stream_check as sc

PROBE = 24


def find_realign(seg, wb, i_r, i_e, max_d=4096):
    """Checker-style realignment: probe at seam skews 0..7."""
    for skew in range(0, 8):
        if i_r + skew + PROBE > len(seg):
            break
        probe = seg[i_r + skew:i_r + skew + PROBE]
        for d in range(0, max_d):
            if i_e + d + PROBE > len(wb):
                break
            if wb[i_e + d:i_e + d + PROBE] == probe:
                return skew, d
    return None


def enumerate_runs(run_bin):
    raw = open(run_bin, "rb").read()
    segments = list(sc.split_segments(raw))
    valid, unparsed = [], []
    for off, seg in segments:
        if not seg:
            continue
        fr = sc.parse_segment(seg)
        if fr is None:
            unparsed.append((off, seg))
        else:
            fr.seg_off = off
            valid.append(fr)
    bases = [f for f in valid if f.flags & sc.FLAG_BURST_FIRST] or valid
    payload_n = sc.Counter(f.frame_len - sc.HDR_LEN - sc.CRC_LEN
                           for f in bases).most_common(1)[0][0]
    fpb = sc.infer_frames_per_burst(valid)

    for off, seg in unparsed:
        if off == 0:
            continue
        if off + len(seg) >= len(raw) and not raw.endswith(b"\x00"):
            continue
        prev = None
        for f in valid:
            if f.seg_off < off:
                prev = f
            else:
                break
        nxt = next((f for f in valid if f.seg_off > off), None)
        span = max(1, min((nxt.seq - prev.seq - 1) if nxt else 3, 16))
        chain = sc.expected_chain(prev, span, payload_n, fpb)
        cand = next(((s, wb) for s, wb in chain if seg[:24] == wb[:24]), None)
        if cand is None:
            print(f"  seg@{off} ({len(seg)}B): fragment, no frame head")
            continue
        seq, wb = cand
        runs = []
        i_r = i_e = 0
        while True:
            n = min(len(seg) - i_r, len(wb) - i_e)
            j = 0
            while j < n and seg[i_r + j] == wb[i_e + j]:
                j += 1
            i_r += j
            i_e += j
            if i_r >= len(seg) or i_e >= len(wb):
                runs.append(("tail", len(seg) - i_r, len(wb) - i_e))
                break
            r = find_realign(seg, wb, i_r, i_e)
            if r is None:
                runs.append(("NO-REALIGN", i_e, len(seg) - i_r))
                break
            skew, d = r
            if d == 0 and skew == 0:
                runs.append(("INPLACE?", i_e))
                break
            runs.append((i_e, d, skew))
            i_e += d
            i_r += skew
        print(f"  seq {seq} (seg@{off}, {len(seg)}B, frame {len(wb)}B): {runs}")


def main():
    runs = sys.argv[1:] or ["ab-4404.bin", "ab-6783.bin", "dual-4404.bin",
                            "dual-6783.bin", "phase-4404.bin", "phase-6783.bin"]
    for r in runs:
        print(f"== {r}")
        enumerate_runs(r)
    return 0


if __name__ == "__main__":
    sys.exit(main())