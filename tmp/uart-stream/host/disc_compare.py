#!/usr/bin/env python3
"""disc_compare.py -- the two-receiver discriminator for issue #190.

Two receivers watch the same target TX pin (P1.04): VCOM1, the Interface
MCU's USB CDC bridge, and a passive FT232 tap.  For every byte range the
VCOM1 capture lost, this tool asks: does the tap's copy of that frame cover
those bytes?

  tap covers them -> the bytes crossed the wire; the loss is AFTER the pin,
                     inside the Interface MCU's VCOM path.
  tap lost them too -> unwitnessed: no verdict for that range either way.

Both captures are walked against the same regenerated frame (canonical
wire coordinates), so ranges from the two sides are directly comparable.
Coverage comes from three sources per frame: fully valid frames (CRC +
payload regeneration), damaged segments whose deleted runs the walk
resolves, and mid-frame fragments attributed by a 24-byte probe (a
xorshift32 payload makes that probe unique).  Anything unattributable is
treated as NOT covered -- conservative for the verdict.

Usage:
  python3 disc_compare.py <vcom1.bin> <tap.bin>
  python3 disc_compare.py --selftest
"""

import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import stream_check as sc

PROBE = 24
MAX_D = 16384        # longest single deleted run the realignment will chase
MAX_SPAN = 64       # largest seq gap treated as one event (not a reboot)


def merge(iv):
    out = []
    for a, b in sorted(iv):
        if out and a <= out[-1][1]:
            out[-1][1] = max(out[-1][1], b)
        else:
            out.append([a, b])
    return [(a, b) for a, b in out]


def complement(iv, length):
    out, pos = [], 0
    for a, b in iv:
        if a > pos:
            out.append((pos, a))
        pos = max(pos, b)
    if pos < length:
        out.append((pos, length))
    return out


def find_realign(seg, wb, i_r, i_e):
    """Checker-style realignment at the divergence point: probe at seam
    skews 0..7 against the expected frame shifted by d."""
    for skew in range(0, 8):
        if i_r + skew + PROBE > len(seg):
            break
        probe = seg[i_r + skew:i_r + skew + PROBE]
        d = wb.find(probe, i_e, i_e + MAX_D)
        if d >= 0:
            return skew, d - i_e
    return None


def body(wb):
    """Canonical frame space = the segment body; wire_bytes() adds the
    trailing 0x00 delimiter that split_segments() strips."""
    return wb[:-1] if wb.endswith(b"\x00") else wb


def analyse(path):
    raw = open(path, "rb").read()
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
    valid.sort(key=lambda f: f.seg_off)
    bases = [f for f in valid if f.flags & sc.FLAG_BURST_FIRST] or valid
    payload_n = sc.Counter(f.frame_len - sc.HDR_LEN - sc.CRC_LEN
                           for f in bases).most_common(1)[0][0]
    fpb = sc.infer_frames_per_burst(valid)

    coverage = {}          # seq -> [(start, end)] covered, canonical wire coords
    frag_unattrib = []     # (capture offset, length)
    frame_len = {}         # seq -> canonical wire length, where known

    def note(seq, wb, ivs):
        frame_len[seq] = len(wb)
        coverage.setdefault(seq, []).extend(ivs)

    # -- unparsed segments: attribute, walk, salvage the rest as fragments --
    for off, seg in unparsed:
        if off == 0:
            frag_unattrib.append((off, len(seg)))
            continue
        if off + len(seg) >= len(raw) and not raw.endswith(b"\x00"):
            frag_unattrib.append((off, len(seg)))   # capture tail
            continue
        prev = None
        for f in valid:
            if f.seg_off < off:
                prev = f
            else:
                break
        nxt = next((f for f in valid if f.seg_off > off), None)
        span = max(1, min((nxt.seq - prev.seq - 1) if nxt else 3, MAX_SPAN))
        chain = [(s, body(wb)) for s, wb in
                 sc.expected_chain(prev, span, payload_n, fpb)]

        cand = next(((s, wb) for s, wb in chain if seg[:PROBE] == wb[:PROBE]), None)
        runs, unknown_from, seq, wb = [], None, None, None
        if cand is not None:
            seq, wb = cand
            i_r = i_e = 0
            while True:
                n = min(len(seg) - i_r, len(wb) - i_e)
                j = 0
                while j < n and seg[i_r + j] == wb[i_e + j]:
                    j += 1
                i_r += j
                i_e += j
                if i_r >= len(seg) or i_e >= len(wb):
                    break
                r = find_realign(seg, wb, i_r, i_e)
                if r is None:
                    unknown_from = i_e
                    break
                skew, d = r
                if d == 0 and skew == 0:
                    unknown_from = i_e
                    break
                runs.append((i_e, i_e + d))     # deleted: NOT covered
                i_e += d
                i_r += skew
            # covered so far: [0, divergence) minus the deleted runs
            note(seq, wb, complement(runs, i_e))
            if unknown_from is not None and i_r < len(seg):
                seg = seg[i_r:]                  # salvage tail as a fragment
            else:
                continue
        # fragment attribution: probe at increasing offsets until a candidate
        # frame matches, then claim only the prefix that verifies against it
        # byte-for-byte (a second deletion inside the fragment would
        # otherwise be overclaimed as covered).
        cands = [cand] if cand is not None else []
        cands += [(s, w) for s, w in chain if not cands or s != cands[0][0]]
        rest = seg
        # A hot-episode segment can carry dozens of small deletions, each
        # ending a verified chunk; 256 rounds covers ~23 per fused frame.
        for _ in range(256):
            if len(rest) < PROBE:
                break
            hit = None
            for k in (0, 8, 16, 24, 32, 48, 64, 96, 128, 192, 256,
                      512, 1024, 2048, 4096):
                if k + PROBE > len(rest):
                    break
                for s, w in cands:
                    p = w.find(rest[k:k + PROBE])
                    if p >= 0:
                        nmax = min(len(rest) - k, len(w) - p)
                        m = sc.first_mismatch(rest[k:], w[p:], nmax)
                        note(s, w, [(p, p + m)])
                        hit = k + m
                        break
                if hit is not None:
                    break
            if hit is None:
                break
            rest = rest[hit:]
        if rest:
            frag_unattrib.append((off, len(rest)))

    # -- frames expected between consecutive valid frames but never seen --
    present = set(coverage)
    missing = []
    for prev, nxt in zip(valid, valid[1:]):
        count = nxt.seq - prev.seq - 1
        if 0 < count <= MAX_SPAN:
            for s, wb in [(s, body(wb)) for s, wb in
                          sc.expected_chain(prev, count, payload_n, fpb)]:
                if s not in present:
                    note(s, wb, [])            # zero coverage
                    missing.append(s)
        elif count > MAX_SPAN:
            print(f"  (gap of {count} seqs after {prev.seq} treated as reboot/"
                  f"outage; not counted)", file=sys.stderr)

    for f in valid:
        note(f.seq, body(sc.wire_bytes(f.seq, f.payload_n, f.sent_total, f.flags,
                                       sc.frame_filler(f.flags))), [(0, f.seg_len)])

    return {
        "valid": valid,
        "coverage": {s: merge(v) for s, v in coverage.items()},
        "frame_len": frame_len,
        "missing": missing,
        "frag_unattrib": frag_unattrib,
    }


def covered(cap, seq, a, b):
    """Bytes of [a,b) that this capture's copy of seq covers."""
    if seq not in cap["coverage"]:
        return 0
    n = 0
    for ca, cb in cap["coverage"][seq]:
        n += max(0, min(b, cb) - max(a, ca))
    return n


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        return selftest()
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a_path, b_path = sys.argv[1], sys.argv[2]
    A, B = analyse(a_path), analyse(b_path)

    a_lost = {s: complement(iv, A["frame_len"][s])
              for s, iv in A["coverage"].items()}
    a_lost = {s: iv for s, iv in a_lost.items() if iv}

    tot_wit = tot_unwit = 0
    n_wit = n_unwit = 0
    partial = []
    for seq in sorted(a_lost):
        L = A["frame_len"][seq]
        for a, b in a_lost[seq]:
            n = b - a
            w = covered(B, seq, a, b)
            if w == n:
                tot_wit += n
                n_wit += 1
            elif w == 0:
                tot_unwit += n
                n_unwit += 1
                partial.append((seq, a, b, 0))
            else:
                tot_wit += w
                tot_unwit += n - w
                n_unwit += 1
                partial.append((seq, a, b, w))

    fa = sum(l for _, l in A["frag_unattrib"])
    fb = sum(l for _, l in B["frag_unattrib"])
    print(f"A ({a_path}): {len(A['valid'])} valid frames, "
          f"{len(A['missing'])} frames never seen, {fa} B in "
          f"{len(A['frag_unattrib'])} unattributable fragment(s)")
    print(f"B ({b_path}): {len(B['valid'])} valid frames, "
          f"{len(B['missing'])} frames never seen, {fb} B in "
          f"{len(B['frag_unattrib'])} unattributable fragment(s)")
    print()
    print(f"A lost {tot_wit + tot_unwit} B across {n_wit + n_unwit} byte ranges:")
    print(f"  witnessed on the wire by B : {tot_wit} B in {n_wit} ranges")
    print(f"  unwitnessed (B lost too)     : {tot_unwit} B in {n_unwit} ranges")
    if partial:
        print("  unwitnessed detail (seq, range, covered B):")
        for seq, a, b, w in partial:
            print(f"    seq {seq}: [{a}, {b}) of {A['frame_len'][seq]}, "
                  f"B covers {w} of {b - a}")
    print()
    pct = 100.0 * tot_wit / (tot_wit + tot_unwit) if tot_wit + tot_unwit else 0.0
    if tot_unwit == 0:
        print("VERDICT: every lost byte was on the wire -- the loss is in A's "
              "receiver path (the Interface MCU's VCOM), not the target.")
    else:
        print(f"VERDICT: {pct:.3f}% of A's lost bytes were on the wire "
              f"(loss in A's receiver path); {tot_unwit} B unwitnessed.")
    return 0


def selftest():
    pn, fpb, n = 2048, 3, 12
    clean, offs = sc.synth_stream(n, pn, fpb)

    def clip(data, seq, start, count):
        o, ln = offs[seq]
        d = bytearray(data)
        del d[o + start:o + start + count]
        return bytes(d)

    def whole_gone(data, seq):
        o, ln = offs[seq]
        return bytes(bytearray(data)[:o] + bytearray(data)[o + ln:])

    checks = []

    def run(name, A_data, B_data, want_line):
        import tempfile, os, subprocess, json
        with tempfile.TemporaryDirectory() as td:
            pa, pb = os.path.join(td, "a.bin"), os.path.join(td, "b.bin")
            open(pa, "wb").write(A_data)
            open(pb, "wb").write(B_data)
            out = subprocess.run([sys.executable, __file__, pa, pb],
                                 capture_output=True, text=True).stdout
        ok = want_line in out
        checks.append((name, ok, [ln for ln in out.splitlines()
                                  if ln.startswith("VERDICT")][0]))
        if not ok:
            print(out)

    run("B clean witnesses A's loss", clip(clean, 5, 1000, 80), clean,
        "every lost byte was on the wire")
    run("B lost the same bytes", clip(clean, 5, 1000, 80),
        clip(clean, 5, 1000, 80), "0.000%")
    run("B lost a disjoint range", clip(clean, 5, 1000, 80),
        clip(clean, 5, 5000, 40), "every lost byte was on the wire")
    run("whole frame lost, B clean", whole_gone(clean, 6), clean,
        "every lost byte was on the wire")

    width = max(len(c[0]) for c in checks)
    bad = 0
    for name, ok, verdict in checks:
        print(f"  {name:<{width}}  {'PASS' if ok else 'FAIL'}  {verdict}")
        bad += not ok
    print(f"selftest: {len(checks) - bad}/{len(checks)} checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())