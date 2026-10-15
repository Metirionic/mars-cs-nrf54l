#!/usr/bin/env python3
"""stream_check.py -- capture and validate the [DEBUG-ust1] UART stream test.

The counterpart to tmp/uart-stream/fw: the firmware streams COBS frames, each
carrying a sequence number, the device's own cumulative wire-byte count, a
content that the host can regenerate from the sequence number alone, and a
CRC32. This tool reads that stream off a serial port (or a saved capture) and
reports every place the bytes on the wire differ from what the device handed
the UART.

Why it is sharp (issue #190):

  * sent_total deltas are byte-exact. Between two consecutive frames the device
    says it emitted N wire bytes; the host counts what actually arrived. The
    difference is the exact number of bytes lost -- or of spurious bytes added
    -- between them. No modal-frame-size inference, no "64-80 bytes short".

  * Content is regenerable. A frame is a pure function of (seq, payload bytes),
    so the host rebuilds the exact expected bytes of any frame and reports
    *where inside the frame* the bytes went missing, not just how many.

  * Loss is separated from corruption. A CRC failure whose decoded content
    still has the expected shape is in-place corruption (a bit flip); a segment
    that does not parse at all, or whose bytes realign later, is dropped bytes.
    #190 needs that distinction.

  * Timing comes along. capture writes a byte-offset -> monotonic-time sidecar,
    so every anomaly carries a wall-clock position in the run.

Subcommands:

  capture PORT --out FILE [--duration S] [--stall-at S --stall-len S]
      Raw-read the port, write FILE plus FILE.tsv (offset, time). Reading is
      non-blocking with a deadline, so an idle port cannot hang the loop.
      --stall-at/--stall-len stop draining the port for a while: that is the
      "host reader blocked" mode of #190 section 2.

  check FILE [--tsv FILE] [--json]
      Validate a capture. Exit 0 = clean, 1 = anomalies, 2 = unreadable.

  selftest
      Regenerate a synthetic capture with known injected defects (a dropped run
      inside a frame, a whole missing frame, a spurious inserted byte, a clipped
      tail) and assert the checker reports exactly those. Proves the signal is
      red-capable without touching hardware.

Stdlib only: there is no pyserial on this bench, and a raw fd is what the
existing #190 harness (tmp/vcom1-diag/) uses too.
"""

import argparse
import bisect
import fcntl
import gc
import contextlib
import io
import json
import os
import select
import struct
import sys
import tempfile
import termios
import time
import zlib
from collections import Counter

# --------------------------------------------------------------------------
# Wire format -- mirrors tmp/uart-stream/fw/src/main.c exactly
# --------------------------------------------------------------------------

MAGIC = 0x4D535431  # "MST1"
VERSION = 1
HDR_LEN = 20        # magic u32, version u8, flags u8, seq u32, frame_len u16, sent_total u64
CRC_LEN = 4
FLAG_BURST_FIRST = 0x01
FLAG_BURST_LAST = 0x02
FLAG_STATUS = 0x04  # the fixed-payload status frame that ends a burst
STATUS_FILLER = 0xA5  # status frames carry this constant, not PRNG data
PAYLOAD_DELTA_B = 3   # the second event frame is this many bytes larger

MASK32 = 0xFFFFFFFF
BAUD = 921600
BITS_PER_BYTE = 10  # 8N1


def prng_seed(seq: int) -> int:
    return (0x9E3779B9 ^ ((seq * 0x85EBCA6B) & MASK32)) & MASK32


def xorshift32(s: int) -> int:
    s &= MASK32
    s ^= (s << 13) & MASK32
    s ^= s >> 17
    s ^= (s << 5) & MASK32
    return s & MASK32


def payload(seq: int, n: int, filler: int = 0) -> bytes:
    """The deterministic payload the firmware fills for this sequence number.

    @param filler  non-zero repeats that constant byte instead (status frames),
                   which makes the frame's wire length a step function of its
                   length alone.
    """
    if filler:
        return bytes((filler,)) * n
    state = prng_seed(seq)
    words = bytearray()
    while len(words) < n:
        state = xorshift32(state)
        words += struct.pack("<I", state)
    return bytes(words[:n])


def build_frame(seq: int, payload_n: int, sent_total: int, flags: int,
                filler: int = 0) -> bytes:
    """Rebuild one decoded frame byte-for-byte."""
    body = bytearray()
    body += struct.pack("<I", MAGIC)
    body += bytes((VERSION, flags))
    body += struct.pack("<I", seq)
    body += struct.pack("<H", HDR_LEN + payload_n + CRC_LEN)
    body += struct.pack("<Q", sent_total)
    body += payload(seq, payload_n, filler)
    body += struct.pack("<I", zlib.crc32(bytes(body)) & MASK32)
    return bytes(body)


def cobs_encode(data: bytes) -> bytes:
    """COBS, mirroring Zephyr's lib/utils/cobs.c byte for byte.

    The subtlety that matters: Zephyr flushes a full 254-byte group only when a
    further byte follows it, and a zero byte always terminates a group --
    emitting its own (possibly empty) group even when that group was just
    flushed full. Both are reproduced here; otherwise the expected stream would
    be a byte off per full group and every diff would light up.
    """
    out = bytearray()
    runs = data.split(b"\x00")
    last_run = len(runs) - 1
    for ri, run in enumerate(runs):
        i = 0
        while len(run) - i > 254:  # a further byte exists, so this group flushes
            out.append(0xFF)
            out += run[i:i + 254]
            i += 254
        rem = run[i:]
        if ri != last_run and len(rem) == 254:
            # zero-terminated on a group boundary: the full group, then the
            # empty group the zero itself emits
            out.append(0xFF)
            out += rem
            out.append(0x01)
        else:
            out.append(len(rem) + 1)
            out += rem
    return bytes(out)


def cobs_decode(seg: bytes):
    """Standard COBS decode; None when the segment is malformed."""
    out = bytearray()
    i, n = 0, len(seg)
    while i < n:
        code = seg[i]
        i += 1
        if code == 0:
            return None
        take = code - 1
        if i + take > n:
            return None
        out += seg[i:i + take]
        i += take
        if code < 0xFF and i < n:
            out.append(0)
    return bytes(out)


def wire_bytes(seq: int, payload_n: int, sent_total: int, flags: int,
               filler: int = 0) -> bytes:
    """One frame as it appears on the wire, trailing delimiter included."""
    return cobs_encode(build_frame(seq, payload_n, sent_total, flags, filler)) + b"\x00"


class Frame:
    """A delimited segment that parsed as a frame."""

    __slots__ = ("seq", "flags", "frame_len", "sent_total", "decoded", "seg_off",
                 "seg_len", "crc_ok", "content_ok", "payload_n", "phase")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


def parse_segment(seg: bytes):
    """Read one delimited segment as a frame. Returns a Frame or None.

    A frame is returned even when its CRC fails -- that is the interesting case.
    The header must still make sense, else the segment is not a frame at all.
    """
    dec = cobs_decode(seg)
    if dec is None or len(dec) < HDR_LEN + CRC_LEN:
        return None
    magic, version, flags, seq, frame_len, sent_total = struct.unpack_from("<IBBIHQ", dec, 0)
    if magic != MAGIC or version != VERSION or frame_len != len(dec):
        return None
    payload_n = frame_len - HDR_LEN - CRC_LEN
    stored_crc, = struct.unpack_from("<I", dec, frame_len - CRC_LEN)
    crc_ok = (zlib.crc32(dec[:frame_len - CRC_LEN]) & MASK32) == stored_crc
    # A status frame's payload is the traffic-pattern phase marker (1 or 2),
    # not data to regenerate -- accept either.
    if flags & FLAG_STATUS and payload_n == 1:
        content_ok = crc_ok and dec[HDR_LEN] in (1, 2)
        phase = dec[HDR_LEN] - 1
    else:
        content_ok = crc_ok and dec[HDR_LEN:HDR_LEN + payload_n] == payload(
            seq, payload_n, frame_filler(flags))
        phase = None
    return Frame(seq=seq, flags=flags, frame_len=frame_len, sent_total=sent_total,
                 decoded=dec, seg_off=0, seg_len=len(seg), crc_ok=crc_ok,
                 content_ok=content_ok, payload_n=payload_n, phase=phase)


def split_segments(raw: bytes):
    """Yield (byte_offset, segment) for every 0x00-delimited body."""
    start = 0
    while True:
        z = raw.find(b"\x00", start)
        if z < 0:
            break
        yield start, raw[start:z]
        start = z + 1
    if start < len(raw):
        yield start, raw[start:]  # trailing partial, no delimiter yet


def first_mismatch(a: bytes, b: bytes, n: int) -> int:
    """Index of the first differing byte within the first n bytes, else n.

    Binary search over prefix equality, so the comparing happens at C speed
    rather than byte by byte in Python.
    """
    lo, hi = 0, n
    while lo < hi:
        mid = (lo + hi) // 2
        if a[:mid + 1] == b[:mid + 1]:
            lo = mid + 1
        else:
            hi = mid
    return lo


def locate_drop(received: bytes, expected: bytes, anchor: int = 24, window: int = 1 << 22):
    """Where inside a damaged frame did the bytes go missing?

    Returns (offset, delta, kind) where offset is the byte position within the
    frame at which received and expected diverge, delta is the number of
    expected bytes absent there (negative: spurious bytes on the wire), and kind
    is 'none' | 'drop' | 'truncated' | 'extra' | 'corrupt'.

    Bytes after a drop realign (the following content is intact, merely
    shifted), which is what separates a dropped run from in-place corruption.

    The realignment probe is tried at a few small skews from the divergence
    point: a dropped run usually leaves one or two mangled bytes at its seam
    (the byte straddling it, and the COBS count byte that followed), and a
    probe anchored exactly on those would miss an otherwise clean deletion.
    """
    n = min(len(received), len(expected))
    off = first_mismatch(received, expected, n)

    if off == n and len(received) == len(expected):
        return off, 0, "none"
    if off == n and len(received) < len(expected):
        return off, len(expected) - len(received), "truncated"

    for skew in range(0, 8):
        if off + skew + anchor > len(received):
            break
        probe = received[off + skew:off + skew + anchor]
        found = expected.find(probe, off + 1, min(len(expected), off + skew + window))
        if found >= 0:
            return off, found - (off + skew), "drop"

    for skew in range(0, 8):
        if off + skew + anchor > len(expected):
            break
        probe = expected[off + skew:off + skew + anchor]
        found = received.find(probe, off + 1, min(len(received), off + skew + window))
        if found >= 0:
            return off, (off + skew) - found, "extra"

    return off, len(expected) - len(received), "corrupt"


def classify_unparsed(seg: bytes, chain):
    """Say what an unparsed segment is, using the regenerated candidates.

    @param chain  [(seq, expected_wire_bytes)] for the sequence numbers this
                  segment sits between.
    """
    found = [(seq, wb, seg.find(wb[:24])) for seq, wb in chain]
    found = [(s, wb, p) for s, wb, p in found if p >= 0]
    if not found:
        # No candidate frame head starts anywhere inside: this is a remnant of
        # the middle or tail of a frame whose head the capture never showed.
        return {"kind": "frame_tail_orphan", "received_len": len(seg)}

    seq0, wb0, pos0 = found[0]
    later = [(s, wb, p) for s, wb, p in found[1:] if p > 0]

    if pos0 == 0 and later:
        s1, _, p1 = later[0]
        return {"kind": "frame_merged", "seq": seq0, "next_seq": s1,
                "cut_at": p1, "frame_wire_len": len(wb0), "received_len": len(seg)}

    if pos0 == 0:
        off, delta, kind = locate_drop(seg, wb0)
        if kind == "drop":
            return {"kind": "frame_drop", "seq": seq0, "first_difference_at": off,
                    "bytes": delta, "frame_wire_len": len(wb0), "received_len": len(seg)}
        if kind == "truncated":
            return {"kind": "frame_split_head", "seq": seq0, "received_len": len(seg),
                    "frame_wire_len": len(wb0),
                    "ends_at_wire_offset": len(seg)}
        return {"kind": "frame_corrupt", "seq": seq0, "first_difference_at": off,
                "delta": delta, "frame_wire_len": len(wb0), "received_len": len(seg)}

    return {"kind": "frame_tail_orphan", "seq_hint": seq0, "received_len": len(seg),
            "starts_at_wire_offset_of_seq": pos0}


STATUS_PAYLOAD_N = 1  # kept in step with CONFIG_STREAM_STATUS_PAYLOAD


def frame_filler(flags: int) -> int:
    """The payload filler a parsed frame's flags imply (status frames only)."""
    return STATUS_FILLER if flags & FLAG_STATUS else 0


def frame_shape(seq: int, period, payload_n: int):
    """(flags, payload bytes, filler) the firmware stamps for this seq.

    A burst is `period` frames: period-1 event frames carrying `payload_n`
    bytes, then a zero-payload status frame with FLAG_STATUS. This mirrors the
    production transfer shape (two SubeventResultEvent frames plus a small
    status frame in one uart_tx).
    """
    if not period:
        return 0, payload_n, 0
    i = (seq - 1) % period
    flags = FLAG_BURST_FIRST if i == 0 else 0
    if i == period - 1:
        flags |= FLAG_BURST_LAST | FLAG_STATUS
        # phase marker 1 = pattern 0; any non-zero value keeps the wire length
        return flags, STATUS_PAYLOAD_N, 1
    return flags, payload_n + (PAYLOAD_DELTA_B if i == 1 else 0), 0


def infer_frames_per_burst(valid):
    """Burst length, from FLAG_BURST_FIRST -> next FLAG_BURST_LAST spans.

    Deliberately not the spacing between consecutive FIRST frames: a damaged or
    dropped frame in between inflates that spacing, which would then mis-stamp
    every regenerated frame's flags byte and defeat the forensics.
    """
    spans, start = [], None
    for f in valid:
        if f.flags & FLAG_BURST_FIRST:
            start = f.seq
        if (f.flags & FLAG_BURST_LAST) and start is not None:
            spans.append(f.seq - start + 1)
            start = None
    if not spans:
        return None
    return Counter(spans).most_common(1)[0][0]


def expected_chain(prev, count, payload_n, frames_per_burst):
    """Regenerate the frames the device would emit after @p prev.

    @param prev  the parsed Frame the capture did show, just before the damaged
                 region: its own sent_total and flags pin the chain down.
    @return      [(seq, wire_bytes)] for seq prev.seq+1 .. prev.seq+count.
    """
    sent = prev.sent_total + len(wire_bytes(prev.seq, prev.payload_n, prev.sent_total,
                                            prev.flags, STATUS_FILLER if prev.flags & FLAG_STATUS else 0))
    out = []
    for seq in range(prev.seq + 1, prev.seq + 1 + count):
        flags, pn, filler = frame_shape(seq, frames_per_burst, payload_n)
        wb = wire_bytes(seq, pn, sent, flags, filler)
        out.append((seq, wb))
        sent += len(wb)
    return out


# --------------------------------------------------------------------------
# capture
# --------------------------------------------------------------------------

def open_raw_port(port: str, baud: int) -> int:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0  # iflag: no CR/NL translation, no IXON/IXOFF
    attrs[1] = 0  # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # 8N1, no CRTSCTS
    attrs[3] = 0  # lflag: raw, no echo
    attrs[4] = attrs[5] = baud
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)  # drop whatever a previous run left
    return fd


TIOCGICOUNT = 0x545D  # struct serial_icounter_struct: the tty layer's own counters
ICOUNT_FIELDS = ("cts dsr rng dcd rx tx frame overrun parity brk buf_overrun").split()


def read_icount(fd):
    """The tty layer's per-port counters, or None if the driver exposes none.

    rx counts bytes the driver handed up; overrun/buf_overrun count bytes the
    KERNEL dropped (hardware overrun / flip-buffer overflow). If a soak loses
    bytes on the wire while these stay zero, the kernel did not drop them --
    which places the loss upstream of the host entirely.
    """
    buf = bytearray(80)
    try:
        fcntl.ioctl(fd, TIOCGICOUNT, buf)
    except OSError:
        return None
    vals = struct.unpack("20i", bytes(buf))
    if not any(vals):
        return None
    return dict(zip(ICOUNT_FIELDS, vals[:11]))


def cmd_capture(args) -> int:
    baud = getattr(termios, "B%d" % args.baud, None)
    if baud is None:
        print(f"unsupported baud {args.baud}", file=sys.stderr)
        return 2

    try:
        fd = open_raw_port(args.port, baud)
    except OSError as e:
        print(f"cannot open {args.port}: {e}", file=sys.stderr)
        return 2

    start = time.monotonic()
    deadline = start + args.duration
    stall_at = start + args.stall_at if args.stall_len > 0 else None
    stalled = False
    total = 0
    events = []
    counters = None

    # A tight read loop on this path returns ~5 bytes per read (the Interface MCU
    # hands the host tiny USB transfers), so it runs ~17k iterations/s: enough
    # GC and filesystem churn to stall the reader for tens of ms, during which
    # downstream buffers overflow and bytes are dropped. That stall pattern, not
    # the device, produced the losses measured with the tight loop.
    gc.disable()

    try:
        with open(args.out, "wb") as fb, open(args.out + ".tsv", "w") as tb:
            # Absolute CLOCK_MONOTONIC at capture start: system-wide, so two
            # captures started on one host can be aligned exactly (event times
            # in the .tsv are relative to this).
            tb.write(f"# start_mono {time.monotonic_ns()}\n")
            while time.monotonic() < deadline:
                now = time.monotonic()
                if stall_at is not None and not stalled and now >= stall_at:
                    # Stop draining: buffers upstream fill and overflow, which
                    # is exactly #190's "host reader blocked" mode.
                    tb.write(f"# stall {args.stall_len}s at byte {total}\n")
                    events.append(f"stall {args.stall_len}s at byte {total}")
                    t0 = time.monotonic()
                    while time.monotonic() - t0 < args.stall_len and time.monotonic() < deadline:
                        time.sleep(0.005)
                    stalled = True
                    tb.write(f"{total}\t{int((time.monotonic() - start) * 1e9)}\tresumed\n")
                    continue

                if args.cadence > 0:
                    # Let the kernel accumulate a useful block before draining.
                    time.sleep(args.cadence / 1000.0)
                else:
                    r, _, _ = select.select([fd], [], [], 0.2)
                    if not r:
                        continue
                try:
                    chunk = os.read(fd, args.chunk)
                except BlockingIOError:
                    continue
                except OSError as e:
                    events.append(f"read failed after {total} bytes: {e}")
                    print(f"read failed after {total} bytes: {e}", file=sys.stderr)
                    break
                if not chunk:
                    continue
                fb.write(chunk)
                total += len(chunk)
                tb.write(f"{total}\t{int((time.monotonic() - start) * 1e9)}\n")
        counters = read_icount(fd)  # before close: counters are per-open
    finally:
        os.close(fd)

    elapsed = time.monotonic() - start
    kbps = total / elapsed / 1000 if elapsed else 0.0
    print(f"{args.port}: {total:,} bytes in {elapsed:.1f}s ({kbps:.1f} KB/s) -> {args.out}")
    for e in events:
        print(f"  {e}")
    if counters is None:
        print("  tty counters : not exposed by cdc_acm on this kernel")
    else:
        drops = counters["overrun"] + counters["buf_overrun"]
        print(f"  tty counters : rx={counters['rx']:,} frame={counters['frame']} "
              f"overrun={counters['overrun']} buf_overrun={counters['buf_overrun']} "
              f"-> kernel dropped {drops} byte(s)")
    return 0


# --------------------------------------------------------------------------
# check
# --------------------------------------------------------------------------

def load_times(tsv_path):
    """(offsets, times_ns) for byte-offset -> wall-clock lookup."""
    offs, times = [], []
    try:
        with open(tsv_path) as f:
            for line in f:
                if line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) < 2:
                    continue
                offs.append(int(parts[0]))
                times.append(int(parts[1]))
    except OSError:
        return [], []
    return offs, times


def offset_time(offs, times, off):
    if not offs:
        return None
    i = min(bisect.bisect_left(offs, off), len(offs) - 1)
    return times[i] / 1e9


def analyse(raw: bytes, tsv_path=None):
    """Validate a capture. Returns the report dict (see cmd_check)."""
    offs, times = load_times(tsv_path) if tsv_path else ([], [])

    segments = list(split_segments(raw))
    valid, broken, unparsed = [], [], []
    for off, seg in segments:
        if not seg:
            continue
        fr = parse_segment(seg)
        if fr is None:
            unparsed.append((off, seg))
            continue
        fr.seg_off, fr.seg_len = off, len(seg)
        (valid if fr.crc_ok else broken).append(fr)

    report = {
        "bytes": len(raw),
        "segments": len(segments),
        "frames_valid": len(valid),
        "frames_corrupt": len(broken),
        "segments_unparsed": len(unparsed),
        "clean": None,
    }
    if not valid:
        return report

    # The BASE event payload comes from the first frame of each burst -- the
    # second one is deliberately larger (PAYLOAD_DELTA_B), so a mode over all
    # event frames would be a tie and could pick either size.
    bases = [f for f in valid if f.flags & FLAG_BURST_FIRST] or valid
    payload_n = Counter(f.frame_len - HDR_LEN - CRC_LEN for f in bases).most_common(1)[0][0]
    frames_per_burst = infer_frames_per_burst(valid)

    frame_len = HDR_LEN + payload_n + CRC_LEN
    report.update({
        "payload_bytes": payload_n,
        "frame_len": frame_len,
        "frames_per_burst": frames_per_burst,
        "seq_first": valid[0].seq,
        "seq_last": valid[-1].seq,
        "payloads_matching_regeneration": sum(1 for f in valid if f.content_ok),
        "device_wire_bytes_at_last_frame": valid[-1].sent_total
        + len(wire_bytes(valid[-1].seq, valid[-1].payload_n, valid[-1].sent_total,
                         valid[-1].flags, frame_filler(valid[-1].flags))),
    })

    # ---- accounting: what the device emitted vs what arrived -------------
    # sent_total is the device's own count of wire bytes handed to the UART
    # before each frame, so the delta between two frames is what it emitted
    # between them; the host counts what arrived. The difference is the loss.
    losses = []
    for a, b in zip(valid, valid[1:]):
        # A reboot restarts seq and sent_total from zero; that counter
        # regression is not a wire anomaly, and treating it as one once
        # produced a "-116 M bytes EXTRA" verdict on a real capture (a
        # mid-capture `nrfutil device reset`). Annotate it separately.
        if b.seq < a.seq or b.sent_total < a.sent_total:
            report.setdefault("device_reboots", []).append({
                "seq_from": a.seq, "seq_to": b.seq,
                "at_offset": b.seg_off,
                "at_s": offset_time(offs, times, b.seg_off),
            })
            continue
        emitted = b.sent_total - a.sent_total
        arrived = b.seg_off - a.seg_off
        if emitted == arrived and b.seq - a.seq == 1:
            continue
        losses.append({
            "kind": "wire_gap",
            "seq_from": a.seq,
            "seq_to": b.seq,
            "frames_missing": max(0, b.seq - a.seq - 1),
            "bytes_emitted": emitted,
            "bytes_arrived": arrived,
            "bytes_lost": emitted - arrived,
            "at_offset": b.seg_off,
            "at_s": offset_time(offs, times, b.seg_off),
            "forensics": [],
        })

    # ---- device accounting self-check ------------------------------------
    # For adjacent frames the delta must equal the wire length the host
    # computes for the earlier one. A mismatch means the firmware's counter --
    # or this tool's mirror of the format -- is wrong, and then the accounting
    # above cannot be trusted, so that is its own loud event.
    accounting_bad = []
    for a, b in zip(valid, valid[1:]):
        if b.seq - a.seq != 1:
            continue  # a gap is the wire_gap check's business, not this one
        expect = len(wire_bytes(a.seq, a.payload_n, a.sent_total, a.flags,
                                frame_filler(a.flags)))
        if b.sent_total - a.sent_total != expect:
            accounting_bad.append({"seq": a.seq,
                                   "device_delta": b.sent_total - a.sent_total,
                                   "host_expected": expect})

    # ---- corruption: frames that parsed but failed CRC -------------------
    for fr in broken:
        exp = build_frame(fr.seq, fr.payload_n, fr.sent_total, fr.flags,
                          frame_filler(fr.flags))
        off, delta, kind = locate_drop(fr.decoded, exp)
        report.setdefault("corrupt", []).append({
            "seq": fr.seq,
            "at_offset": fr.seg_off,
            "at_s": offset_time(offs, times, fr.seg_off),
            "decoded_len": len(fr.decoded),
            "expected_len": len(exp),
            "first_difference_at": off,
            "delta": delta,
            "shape": kind,
        })

    # ---- forensics for segments that did not parse as a frame ------------
    # These are the #190 signature: a frame with bytes dropped out of it (so
    # its COBS body no longer decodes to its own frame_len), or two frames
    # fused when a delimiter went missing. Identify each one against the
    # regeneration of the sequence numbers it sits between, then locate the
    # divergence byte-exactly on the wire.
    for off, seg in sorted(unparsed):
        # The first and last segments are partial by construction: the capture
        # started mid-frame and stopped mid-frame. Never damage.
        if off == 0:
            report["leading_partial_bytes"] = len(seg)
            continue
        if off + len(seg) >= len(raw) and not raw.endswith(b"\x00"):
            report["trailing_partial_bytes"] = len(seg)
            continue

        prev = None
        for f in valid:
            if f.seg_off < off:
                prev = f
            else:
                break
        if prev is None:
            report["leading_partial_bytes"] = len(seg)
            continue

        nxt = next((f for f in valid if f.seg_off > off), None)
        span = max(1, min((nxt.seq - prev.seq - 1) if nxt else 3, 16))
        chain = expected_chain(prev, span, payload_n, frames_per_burst)
        event = classify_unparsed(seg, chain)
        event.update({"at_offset": off, "at_s": offset_time(offs, times, off)})

        # Attach this forensic detail to the accounting event covering it; a
        # segment with no accounting event of its own is reported on its own.
        seq_hint = event.get("seq", event.get("seq_hint", prev.seq + 1))
        for loss in losses:
            if loss["seq_from"] <= seq_hint <= loss["seq_to"]:
                loss["forensics"].append(event)
                break
        else:
            report.setdefault("orphan_forensics", []).append(event)

    # ---- burst geometry ---------------------------------------------------
    # The device's own sent_total makes each uart_tx's total size measurable
    # from the wire alone: production hands the UART 26,658 B per transfer
    # (two event frames + a status frame), so this is the fidelity check.
    totals, first_sent, since = [], None, 0
    for f in valid:
        if f.flags & FLAG_BURST_FIRST:
            first_sent, since = f.sent_total, 1
            continue
        if first_sent is None:
            continue
        since += 1
        # Only count bursts whose frames all arrived: a damaged frame is the
        # very thing being measured, and would skew the geometry statistics.
        if (f.flags & FLAG_STATUS) and frames_per_burst and since == frames_per_burst:
            filler = frame_filler(f.flags)
            totals.append(f.sent_total
                          + len(wire_bytes(f.seq, f.payload_n, f.sent_total, f.flags, filler))
                          - first_sent)
            first_sent = None
    if totals:
        modal_total = Counter(totals).most_common(1)[0][0]
        report["burst_bytes_modal"] = modal_total
        report["burst_bytes_mean"] = round(sum(totals) / len(totals), 2)
        report["burst_bytes_range"] = [min(totals), max(totals)]
        report["burst_bytes_at_modal"] = f"{Counter(totals)[modal_total]}/{len(totals)}"

    # ---- arrival timing --------------------------------------------------
    slow = []
    for a, b in zip(valid, valid[1:]):
        ta, tb = offset_time(offs, times, a.seg_off), offset_time(offs, times, b.seg_off)
        if ta is None or tb is None:
            continue
        wire = len(wire_bytes(a.seq, a.payload_n, a.sent_total, a.flags,
                              frame_filler(a.flags)))
        expect_s = wire * BITS_PER_BYTE / BAUD
        if tb - ta > max(0.5, expect_s * 20):
            slow.append({"seq_from": a.seq, "seq_to": b.seq, "gap_s": round(tb - ta, 3),
                         "expected_s": round(expect_s, 4)})
    if slow:
        report["slow_arrivals"] = slow[:20]

    # Attribute each loss to the traffic pattern in force: the status frame of
    # the same burst carries the phase marker.
    for loss in losses:
        near = min((f for f in valid if f.phase is not None),
                   key=lambda f: abs(f.seq - loss["seq_to"]), default=None)
        loss["phase"] = near.phase if near is not None else None
    by_phase = Counter(l["phase"] for l in losses)
    if any(k is not None for k in by_phase):
        spans = Counter(f.phase for f in valid if f.phase is not None)
        # bursts, not frames: three frames per burst
        bursts_seen = {k: v / 3 for k, v in spans.items()}
        report["losses_by_phase"] = {
            str(k): {"events": by_phase[k],
                     "bursts": round(bursts_seen.get(k, 0), 1),
                     "events_per_100_bursts": round(100.0 * by_phase[k] / bursts_seen[k], 2)
                     if bursts_seen.get(k) else None}
            for k in by_phase}

    report["anomalies"] = losses
    if accounting_bad:
        report["device_accounting_mismatch"] = {"count": len(accounting_bad),
                                                "examples": accounting_bad[:5]}
    report["bytes_lost"] = sum(x["bytes_lost"] for x in losses)
    report["clean"] = not (losses or broken or accounting_bad
                           or report.get("orphan_forensics"))
    return report


def cmd_check(args) -> int:
    try:
        with open(args.capture, "rb") as f:
            raw = f.read()
    except OSError as e:
        print(f"cannot read {args.capture}: {e}", file=sys.stderr)
        return 2

    if not args.tsv and not os.path.exists(args.capture + ".tsv"):
        tsv = None
    else:
        tsv = args.tsv or args.capture + ".tsv"

    rep = analyse(raw, tsv)

    if args.json:
        print(json.dumps(rep, indent=2))
        return 0 if rep.get("clean") else 1

    if rep.get("frames_valid", 0) == 0:
        print(f"{args.capture}: no valid frame in {rep['bytes']:,} bytes "
              f"({rep['segments']} segments, {rep['segments_unparsed']} unparseable)")
        return 2

    # throughput from the sidecar, when there is one
    offs, times = load_times(tsv) if tsv else ([], [])
    dur = (times[-1] - times[0]) / 1e9 if len(times) > 1 else 0.0
    rate = f", {rep['bytes'] / dur / 1000:.1f} KB/s" if dur else ""

    print(f"capture   : {args.capture} ({rep['bytes']:,} bytes{rate})")
    print(f"geometry  : event frame payload {rep['payload_bytes']} B, decoded "
          f"{rep['frame_len']} B; {rep['frames_per_burst'] or '?'} frames/burst "
          f"(last one the {STATUS_PAYLOAD_N}-byte status frame)")
    if rep.get("burst_bytes_modal") is not None:
        print(f"burst     : mean {rep['burst_bytes_mean']:,.1f} B, modal "
              f"{rep['burst_bytes_modal']:,} ({rep['burst_bytes_at_modal']}); range "
              f"{rep['burst_bytes_range'][0]:,}-{rep['burst_bytes_range'][1]:,}; "
              f"production logs 26,658")
    print(f"frames    : {rep['frames_valid']:,} valid, {rep['frames_corrupt']} corrupt, "
          f"{rep['segments_unparsed']} did not parse; seq {rep['seq_first']}..{rep['seq_last']}")
    print(f"content   : {rep['payloads_matching_regeneration']:,}/{rep['frames_valid']:,} "
          f"payloads match regeneration of their seq "
          f"({100.0 * rep['payloads_matching_regeneration'] / rep['frames_valid']:.3f}%)")
    print(f"device    : {rep['device_wire_bytes_at_last_frame']:,} wire bytes stamped "
          f"by the device's own accounting")

    if rep.get("device_accounting_mismatch"):
        m = rep["device_accounting_mismatch"]
        print(f"  BAD     device byte counter disagrees with this tool's format mirror in "
              f"{m['count']} place(s), e.g. seq {m['examples'][0]['seq']}: device "
              f"{m['examples'][0]['device_delta']} vs host {m['examples'][0]['host_expected']}"
              f"  <-- report unreliable, fix the mirror first")

    if rep.get("device_reboots"):
        for r in rep["device_reboots"]:
            t = f" @ {r['at_s']:.2f}s" if r["at_s"] is not None else ""
            print(f"reboot   : device restarted mid-capture (seq {r['seq_from']} -> "
                   f"{r['seq_to']}){t} -- counter regression, not a wire anomaly")

    if rep["clean"]:
        print("anomalies : none")
        print("VERDICT   : PASS -- every byte the device emitted arrived, in order, intact")
        return 0

    if rep.get("losses_by_phase"):
        for k in sorted(rep["losses_by_phase"]):
            v = rep["losses_by_phase"][k]
            name = {"0": "one uart_tx per burst", "1": "one uart_tx per frame"}.get(k, k)
            print(f"phase {k} ({name}): {v['events']} event(s) over {v['bursts']:,.0f} bursts "
                  f"= {v['events_per_100_bursts']} per 100 bursts")
    print(f"anomalies : {len(rep['anomalies'])} loss event(s), "
          f"{rep['frames_corrupt']} corrupt frame(s), {rep['bytes_lost']:+d} bytes net")
    for i, loss in enumerate(rep["anomalies"], 1):
        t = f" @ {loss['at_s']:.2f}s" if loss["at_s"] is not None else ""
        direction = "LOST" if loss["bytes_lost"] >= 0 else "EXTRA"
        print(f"  {i}. {direction} {abs(loss['bytes_lost']):,} B between seq "
              f"{loss['seq_from']} and {loss['seq_to']} ({loss['frames_missing']} frame(s) "
              f"missing or damaged; device emitted {loss['bytes_emitted']:,}, host received "
              f"{loss['bytes_arrived']:,}){t}")
        for f in loss["forensics"]:
            if f["kind"] == "frame_drop":
                print(f"     └ seq {f['seq']}: {f['bytes']:,} B missing at wire offset "
                      f"{f['first_difference_at']:,} of {f['frame_wire_len']:,}, content "
                      f"realigns after it")
            elif f["kind"] == "frame_merged":
                print(f"     └ seq {f['seq']}: cut at wire offset {f['cut_at']:,} of "
                      f"{f['frame_wire_len']:,}, seq {f['next_seq']} fused straight on "
                      f"(delimiter lost)")
            elif f["kind"] == "frame_split_head":
                print(f"     └ seq {f['seq']}: segment ends at wire offset "
                      f"{f['ends_at_wire_offset']:,} of {f['frame_wire_len']:,}, no "
                      f"realignment (frame split here)")
            elif f["kind"] == "frame_corrupt":
                print(f"     └ seq {f['seq']}: first difference at wire offset "
                      f"{f['first_difference_at']:,}, no realignment (in-place damage)")
            elif f["kind"] == "frame_tail_orphan":
                print(f"     └ fragment of {f['received_len']:,} B matching no frame head "
                      f"(middle/tail of a frame)")
            else:
                print(f"     └ {f['kind']} at byte {f['at_offset']:,} "
                      f"({f['received_len']:,} B)")

    for c in rep.get("corrupt", []):
        t = f" @ {c['at_s']:.2f}s" if c["at_s"] is not None else ""
        print(f"  C. seq {c['seq']}: CRC failed, no byte loss ({c['decoded_len']} B decoded, "
              f"first difference at {c['first_difference_at']:,}, shape {c['shape']}){t}")

    for f in rep.get("orphan_forensics", []):
        print(f"  ?. unowned damage at byte {f['at_offset']:,}: {f['kind']}")

    if rep.get("slow_arrivals"):
        print(f"slow      : {len(rep['slow_arrivals'])} arrival gap(s) far above wire time, "
              f"worst {max(s['gap_s'] for s in rep['slow_arrivals']):.2f}s "
              f"(wire time {rep['slow_arrivals'][0]['expected_s']}s) -- device-side stall or "
              f"a reader that stopped draining")

    print(f"VERDICT   : FAIL -- {rep['bytes_lost']:+d} bytes net, "
          f"{rep['frames_corrupt']} corrupt frame(s)")
    return 1


# --------------------------------------------------------------------------
# selftest -- prove the checker is red-capable, no hardware involved
# --------------------------------------------------------------------------

def synth_stream(n_frames, payload_n, frames_per_burst):
    """A faithful capture: frames 1..n with correct sent_total stamps."""
    out = bytearray()
    sent = 0
    offsets = {}
    for i in range(1, n_frames + 1):
        flags, pn, filler = frame_shape(i, frames_per_burst, payload_n)
        wb = wire_bytes(i, pn, sent, flags, filler)
        offsets[i] = (len(out), len(wb))
        out += wb
        sent += len(wb)
    return bytes(out), offsets


def cmd_selftest(args) -> int:
    payload_n = 2048  # smaller than the bench default: this is about logic
    n, fpb = 9, 3     # 3 = two event frames + the status frame, as on the bench
    clean, offs = synth_stream(n, payload_n, fpb)
    checks = []

    def run(name, data, want_exit, want_in=(), want_not_in=()):
        with tempfile.NamedTemporaryFile(prefix=f"ustream-{name}-", suffix=".bin",
                                         delete=False) as tf:
            tf.write(data)
            path = tf.name
        ns = argparse.Namespace(capture=path, tsv=None, json=False)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = cmd_check(ns)
        out = buf.getvalue()
        ok = rc == want_exit and all(s in out for s in want_in) and \
            not any(s in out for s in want_not_in)
        verdict = [ln for ln in out.splitlines() if ln.startswith("VERDICT")]
        checks.append((name, ok, rc, verdict[0] if verdict else out.strip().splitlines()[-1]))
        if not ok:
            checks[-1] = (name, ok, rc, verdict[0] if verdict else "")
            print(f"--- {name} (unexpected) ---\n{out}")
        os.unlink(path)

    run("clean", clean, 0, ["PASS", "100.000%"], want_not_in=["match regeneration of their seq (9"])
    run("clipped_tail", clean[:offs[n][0] + offs[n][1] // 2], 0, ["PASS"])  # normal at capture end

    o, ln = offs[4]
    d = bytearray(clean)
    del d[o + 1000:o + 1080]  # 80 B dropped inside a frame: the #190 shape
    run("drop_in_frame", bytes(d), 1, ["LOST 80 B", "seq 4", "realigns"])

    o, ln = offs[5]
    d = bytearray(clean)
    del d[o:o + ln]  # a whole frame gone
    run("frame_missing", bytes(d), 1, ["1 frame(s) missing or damaged"])

    o, ln = offs[3]
    d = bytearray(clean)
    d[o + 100:o + 100] = b"\x00"  # a spurious byte, the marginal-FTDI shape
    run("spurious_byte", bytes(d), 1, ["EXTRA 1 B"])

    o, ln = offs[6]
    d = bytearray(clean)
    del d[o + ln - 1]  # drop a delimiter: two frames fuse (the customer's merge)
    run("merged_frames", bytes(d), 1, ["fused", "delimiter lost"])

    width = max(len(c[0]) for c in checks)
    bad = 0
    for name, ok, rc, verdict in checks:
        print(f"  {name:<{width}}  {'PASS' if ok else 'FAIL'}  exit {rc}   {verdict}")
        bad += not ok
    print(f"selftest: {len(checks) - bad}/{len(checks)} checks passed")
    return 1 if bad else 0


# --------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("capture", help="read a serial port into a capture file")
    c.add_argument("port")
    c.add_argument("--out", required=True)
    c.add_argument("--duration", type=float, default=60.0)
    c.add_argument("--stall-at", type=float, default=0.0)
    c.add_argument("--stall-len", type=float, default=0.0)
    c.add_argument("--chunk", type=int, default=65536)
    c.add_argument("--cadence", type=float, default=0.0,
                   help="ms to let the kernel buffer before each read. 0 = tight "
                        "loop (read whatever arrives, ~5 B at a time on this path, "
                        "which churns the reader and can stall it); 20 batches to ~1.6 KB "
                        "per read and is what a sane host tool does.")
    c.add_argument("--baud", type=int, default=BAUD)

    k = sub.add_parser("check", help="validate a capture")
    k.add_argument("capture")
    k.add_argument("--tsv", default=None)
    k.add_argument("--json", action="store_true")

    sub.add_parser("selftest", help="prove the checker catches injected defects")

    args = ap.parse_args()
    return {"capture": cmd_capture, "check": cmd_check, "selftest": cmd_selftest}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
