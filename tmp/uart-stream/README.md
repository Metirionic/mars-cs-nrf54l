# UART stream test (issue #190)

A Bluetooth-free streaming test for the COBS UART path, built to narrow #190.
The firmware emits sequence-numbered, CRC'd COBS frames on `uart20` — the exact
UART, pins, baud and `uart_tx` pattern production uses — and the host checks
every byte that arrives against what the device says it sent.

Nothing here touches Channel Sounding, BLE, or the Rust serializer: the UART
path is the only thing under test.

```
tmp/uart-stream/
├── fw/        Zephyr app: the stream generator (portable to any carrier board)
├── probe/     scratch app: uart_tx size sweep + cache-coherence A/B checks
├── host/stream_check.py   capture + validate (stdlib only)
└── flash.sh   flash and PROVE the image is the one running
```

## Wire format

One frame, COBS-encoded with a trailing `0x00` delimiter:

| off | size | field | notes |
|-----|------|-------|-------|
| 0 | 4 | magic | `'M','S','T','1'` |
| 4 | 1 | version | 1 |
| 5 | 1 | flags | bit0 first of burst, bit1 last, bit2 status frame |
| 6 | 4 | seq | frame sequence, from 1 |
| 10 | 2 | frame_len | decoded length, header + payload + crc |
| 12 | 8 | sent_total | **wire bytes handed to `uart_tx` before this frame** |
| 20 | N | payload | PRNG stream seeded by `seq` (status frames: constant filler) |
| 20+N | 4 | crc32 | zlib/IEEE-802.3 over all preceding bytes |

A **burst** is one `uart_tx` and mirrors production's transfer shape — two event
frames then a small status frame:

| frame | payload | decoded | wire (modal) |
|-------|---------|---------|--------------|
| event A | 13,258 | 13,282 | 13,316 |
| event B | 13,261 (+3, as production's two event frames differ) | 13,285 | 13,318 |
| status | 1 (`0xA5` filler) | 25 | 26 |
| **burst** | | | **mean 26,658** — production's logged `uart_tx` size |

Production logs 26,658 B in 226 of 227 procedures. The status frame carries a
*constant* filler rather than PRNG data, so its wire length moves exactly 1:1
with `STREAM_STATUS_PAYLOAD` — that is the fine adjustment landing the total on
26,658.

**Known fidelity gap:** per-frame wire lengths jitter ~±10 B (the burst's modal
total consequently wanders 26,657–26,659 by window), where production jitters
~±1. The cause is the payload: PRNG bytes put the COBS count bytes in different
places every frame, while production's step arrays are ~29% fixed zero padding
(`mode1` bloat, mars-bluetooth-hci#40) with a stable zero structure. Closing
that gap means making the payload mostly fixed zeros — at the cost of the
loss-*location* forensics, which need non-repeating content to realign against.
Forensics won; say the word if you want the production-statistics variant instead.

Every byte of a frame is a pure function of `(seq, payload bytes)`. That is
deliberate — it is what lets the host regenerate the exact expected wire stream
and say *where inside a frame* bytes went missing. A device timestamp field was
in the first draft and had to go: COBS expansion depends on where a field's
zero bytes land, so a never-received frame's wire length would be unknowable.

## Why this is sharper than the COBS harness in `tmp/vcom1-diag/`

- **`sent_total` deltas are byte-exact.** Between two consecutive frames the
  device says it emitted N wire bytes; the host counts what arrived. The
  difference is the exact number lost (or spurious). No modal-frame-size
  inference, no "64–80 bytes short".
- **Content is regenerable**, so a damaged frame gets a byte offset in the
  frame, not just a length: "83 B missing at wire offset 5,681 of 13,313,
  content realigns after it".
- **Loss is separated from corruption.** Content that realigns after the gap is
  dropped bytes; content that never realigns is in-place damage (a bit flip).
  #190 needs that distinction.
- **Spurious bytes are caught too** (a spurious `0x00` shows as `EXTRA 1 B`),
  which the old harness could only see as a mysterious frame split.

## Use

```bash
# flash a verified image (retries until the console proves it is running)
./tmp/uart-stream/flash.sh 1057744404 tmp/uart-stream/build/fw/zephyr/zephyr.hex \
    "DEBUG-ust1] uart-stream"

# the loop: capture, then check. Exit 0 = clean, 1 = anomalies.
python3 tmp/uart-stream/host/stream_check.py capture /dev/ttyACM5 \
    --out tmp/uart-stream/run.bin --duration 60
python3 tmp/uart-stream/host/stream_check.py check tmp/uart-stream/run.bin

# the "host reader blocked" mode of #190 section 2
python3 tmp/uart-stream/host/stream_check.py capture /dev/ttyACM5 --out stall.bin \
    --duration 30 --stall-at 10 --stall-len 3

# prove the checker goes red on injected defects -- no hardware needed
python3 tmp/uart-stream/host/stream_check.py selftest
```

`check` also works on any saved capture, and `--json` emits the full report.

## Streaming geometry

Build-time Kconfig, swept with `west build ... -- -DCONFIG_...=`:

| knob | default | mirrors |
|------|---------|---------|
| `STREAM_PAYLOAD_BYTES` | 13258 | event frame A: 13,316 B on the wire |
| `STREAM_PAYLOAD_DELTA_B` | 3 | production's two event frames differ by 3 B |
| `STREAM_DATA_FRAMES_PER_BURST` | 2 | a status frame always follows |
| `STREAM_STATUS_PAYLOAD` | 1 | tuned so the burst total is exactly 26,658 |
| `STREAM_GAP_MS` | 36 | ~89% duty at 921600, as shipped (measured 87.9%) |
| `STREAM_BURST_COUNT` | 0 | 0 = stream until reset |
| `STREAM_STATS_PERIOD_S` | 0 | 0 = console silent after the banner |

Console traffic shares the Interface MCU with the stream (VCOM0 vs VCOM1 on one
probe) and is itself a suspect in #190, so the default keeps the console quiet
except for a one-line banner that identifies the build. `flash.sh` uses that
banner to verify the flash.

## Transfer-granularity A/B: no effect

The firmware can alternate, every 64 bursts, between production's shape (the
whole burst in one `uart_tx`) and one `uart_tx` per frame, stamping the phase
into the status frame so a *single* soak compares both patterns under identical
bench conditions -- the loss is episodic, so two separate runs cannot be
compared. 600 s, both DKs simultaneously:

| board | one `uart_tx` per burst | one `uart_tx` per frame |
|-------|------------------------|------------------------|
| \*4404 | 11 events / 301 bursts (3.66 per 100) | 9 / 307 (2.93 per 100) |
| \*6783 | 13 events / 310 (4.20 per 100) | 8 / 303 (2.64 per 100) |

Both differences are noise (0.45σ and 1.09σ). **How we hand the bytes to the
UART is not a lever** -- so the workaround cannot be "chunk the transfer
differently".

The same run also re-confirms the shared trigger at scale: with 20 and 21
events on two independently-running boards, **16 of 20 events pair across
boards within 0.5 s, median offset 52 ms** (independence would predict ~0.7
matches). Two boards cannot synchronise by chance, so whatever fires is shared.

Loss magnitude in this run: mostly 61-97 B, but with outliers of 6 B and 348 B --
the "~80 B quantum" is the mode, not a constant, which matters for sizing any
erasure-coding workaround.

## Bugs this harness caught in its own firmware

Worth recording, because each produced a *plausible-looking* signal rather than
an obvious failure:

1. **`frame_buf` sized for the base payload only.** Adding the +3-byte second
   event frame let `build_frame` write 3 bytes past the end of the array -- no
   fault, no assert, just a malformed first frame in every burst. The checker
   saw it as 88 unparsed segments and a two-thirds content-match rate. Now
   `BUILD_ASSERT` + `__ASSERT` bound it.
2. **The idle gap overlapped the transmission.** `k_msleep(gap)` sat *after*
   `uart_tx()` but *before* waiting for TX_DONE, so it was absorbed by the
   289 ms transfer: the line ran at ~99% duty, 91 KB/s, and the configured gap
   had no effect at any value (200 ms measured identical to 36 ms). The gap now
   follows the completed transfer -- 87.9% duty, 81 KB/s.

## Two tooling traps this harness now closes

Both cost real time during the build-out; they are baked into the tooling.

1. **An app-root overlay is silently ignored.** Zephyr only searches
   `<root>/boards/` for `<board>.overlay`, so the UART overlay must live in
   `fw/boards/` and the app dir must be a `BOARD_ROOT` (see `fw/CMakeLists.txt`,
   the same idiom `initiator/CMakeLists.txt` uses for the repo's `boards/`).
   Without it the DK's own dtsi leaves `uart20` at **115200** and points
   `zephyr,console` at `uart20`, while the Interface MCU reads 921600 — the
   stream arrives as 8×-oversampled garbage and the console goes to the wrong
   VCOM. The first version of this app never applied its overlay and tested
   nothing.

2. **`nrfutil device program` lies.** It exits 0 while leaving the previous
   image in place, and it leaves the core halted, so a silent console looks
   exactly like a hung firmware. Always `nrfutil device reset` after flashing
   and verify the banner on the wire — that is what `flash.sh` automates.

## Verification addenda (2026-10-11, independent re-check session)

Two harness hardenings made during verification (both additive; selftest still 6/6):

1. **`capture` writes `# start_mono <ns>` as the first `.tsv` line** — the
   absolute CLOCK_MONOTONIC at capture start. The per-read timestamps remain
   relative to it, so two captures launched on one host can now be aligned
   exactly instead of assuming simultaneous launch (the 16-of-20 pairing was
   measured under that assumption; the fresh re-run with exact alignment
   reproduces it — see `reverify/`).
2. **`check` detects device reboots** (seq or `sent_total` regressing) and
   annotates them instead of reporting a counter regression as a giant
   negative "EXTRA" event — `phase-6783.bin` contains a mid-capture
   `nrfutil device reset` that previously produced a "-116 M bytes EXTRA"
   verdict. The 2 genuine losses in that capture were always valid.

New analysis tool: `host/pair_analysis.py` — cross-board event pairing (with a
tolerance/offset scan that shows the pairing is not offset-fishing), the ~6 s
grid fit, the bracketing read-gap measurement at each divergence byte, and the
phase-marker alternation check. `host/multi_run.py` enumerates *every* deleted
run inside a damaged frame (the checker's forensics locate only the first).

### What the re-check found in the existing captures

- **Reproduced exactly:** the ab-run numbers (20/21 events, 11-9 and 13-8 by
  phase, 0 corrupt frames, 100 % payload match, burst mean 26,658.2), the
  16-of-20 pairing (median 51 ms, all 16 within ±0.10 s, and *no* shift over
  ±2 s beats the zero-shift match count), the C4 phase marker (exactly 29
  switches at exactly 192-frame spacing on both boards), and the C6
  bracketing gaps (0.01-0.55 ms tight-loop / 20.1 ms designed cadence; run
  max read-gap 76 ms).
- **Refined:** 40 of 42 damaged frames are a single contiguous deletion, but
  two (phase run, seq 9727 and 989) each carry *two* deleted runs ~150-180 B
  apart in the same frame (19+74 and 25+101, matching the accounting exactly).
  "One contiguous run per event" is the norm, not a law.
- **Two events resist the pure-deletion model** (ab run: \*4404 seq 407, 300 B
  lost; \*6783 seq 4736, 73 B lost): after the divergence byte the received
  tail never realigns with the regenerated frame anywhere — instead ~64
  alien bytes (matching nothing in the frame) sit at the seam and the content
  resumes from ~364 B / ~137 B later in the frame. Net loss matches the
  accounting, so these are drop+splice events, not in-place bit errors (0
  frames failed CRC). See the session report for details.
- The ~6 s "grid" is a dominant spacing, not a strict grid: a least-squares
  fit over the ab-run episode gives P = 5.96-5.98 s with residuals up to
  ±1.8 s — much looser than the hot-run's ±0.01 s stretch that the "~0.08 %
  drift" figure came from. The shared-trigger conclusion does not depend on
  the grid being strict (the pairing is event-by-event).

### OB firmware matrix (2026-10-11 evening) — the IMCU firmware owns the VCOM path

The A/B the whole issue hinges on, run with the harness on both boards. Firmwares
are SEGGER "J-Link OB-nRF5340-NordicSemi" images (IMCU = nRF5340, USB full
speed), flashed via `nrfutil device x-update-debug-probe-firmware --jlink-dll
<path>` (flashes the DLL's bundled image; **prints "Unspecified error" after
"Could not read J-Link capabilities" even when the flash succeeds** — verify
with the JLinkExe banner AND the echoed S/N, never the banner alone).

**Downgrading is possible — corrected 2026-10-15.** The claim that stood here
until now ("the Jul-8-2025 image is not recoverable; SEGGER serves only the
latest package") was wrong twice over. SEGGER's download page *does* serve older
packs — a version picker down to V5.00, 505 versioned `JLink_Linux_V###_x86_64.deb`
URLs from V6.00h to V9.84 — and an archived version downloads fine through the
browser's license-accept POST (plain `curl` gets HTTP 405 + an AWS WAF captcha,
which is what made the archive look closed). And the user **did** downgrade
\*6783 to the Jul-8-2025 image on a separate PC on 2026-10-15. What failed *on
this bench* was only the 2023-era tooling: the installed V7.92m DLL will not
drive the post-Jun-2026 bootloader (and its JLinkExe crashes on modern glibc,
FD_SETSIZE fortify), so the Oct-17-2023 image bundled with it is not a usable
downgrade target from here.

| OB firmware | stream baud | delivery |
|---|---|---|
| Jul-8-2025 (as shipped) | 921600 | 99.998% intact, sporadic ~76 B deletions (all prior results) |
| Jul-8-2025 (restored 2026-10-15) | 921600 | **PASS**: 81.5 KB/s, 1,650/1,650 payloads byte-exact, 0 corrupt, `sent_total` exact over 180 s (raw capture `runs/restored-2026-10-15/`) |
| Jul-8-2025 (restored) | **1 Mbps** | **no valid frame at all** (2026-10-15: VCOM1 delivered 13.6 KB/s of framing-error garbage — flat-random byte histogram, 0 of 8 segments parse — while the FT232 tap on the same pin was **PASS at 85.2 KB/s, 76/76 frames, 0 corrupt**: the SoC and line are byte-perfect at 1 Mbps) |
| Jun-11-2026 (V9.84 pack) | 921600 | **broken**: ~45% rate, 0 parseable event frames, long frames damaged mid-frame (frame heads present — 87 '1TSM' magics per 20 s; only the 26 B status frames survive) |
| Jun-11-2026 | 460800 | **broken** the same way (106 heads / 30 s, 10 full frames of 147, all survivors byte-exact) |
| Jun-11-2026 | 115200 | **perfect**: PASS, 18/18 frames, 100% match (but 11 KB/s — unusable for production's 81 KB/s) |
| Jun-11-2026 | **1 Mbps** | **broken** the same way (2026-10-14, this row: FT232 tap on P1.04 byte-perfect at 85.3 KB/s / 1,152 frames / 0 host drops, while VCOM1 delivered **36.8 KB/s**, 17 valid frames, 265 unparsed) |

**The new OB firmware's VCOM1 has a hard throughput ceiling, not a baud problem.**
It delivers ~37 KB/s whatever the configured rate: 37.2 KB/s at 921600 and 36.8 at
1 Mbps, with the offered rate rising from 81 to 85 KB/s in between. That single
number explains every row above — 115200 (11 KB/s offered) fits under it and is
perfect, 460800 (46 KB/s), 921600 (82) and 1 Mbps (85) all exceed it and break. So
"up to 1 Mbps" in Nordic's documentation describes which *baud* the debugger will
accept, not the throughput its VCOM can carry. The damage is drop, not corruption:
surviving frames are byte-exact, and the external tap on the same pin sees every
byte (kernel counters: frame=0 overrun=0 buf_overrun=0).

**1 Mbps is unusable through VCOM1 on *either* OB image, in two different ways**
(2026-10-15): the Jul-2025 image delivers framing-error garbage — zero valid
frames, as if its VCOM does not properly apply the 1 Mbps line coding — while the
Jun-2026 image accepts the rate but caps throughput at ~37 KB/s. The wire is
innocent both times: the FT232 tap on the same pin parses every frame byte-exact
at 85.2–85.3 KB/s. Practical guidance for DK users: stream through VCOM1 at
921600 (tested; lower bauds untested but strictly less stress, on the as-shipped OB image), and use an external USB-UART bridge
for 1 Mbps. The 1 Mbps app image is kept at `build/fw-1m`; the bench was restored
to the 921600 production geometry afterwards (reflash via the staged same-version
DLL — see `OB-firmware-downgrade.md`).

The damage is baud-dependent and length-dependent: the new OB's VCOM RX cannot
sustain high-rate long frames (mid-frame corruption), while low-rate delivery is
byte-perfect. **Customer guidance: do NOT update the DK's debug-probe firmware
on this bench's evidence — the newest OB is a large regression for VCOM1
streaming at 921600.**

Both boards are now on Jun-11-2026 (an `x-update-debug-probe-firmware` run
targeting \*6783 by S/N flashed *both* probes — the probe re-enumerates
mid-update and the DLL re-attaches; the bench memory's "check the echoed S/N"
warning applies to the *update path* too). \*6783's app was rebuilt at each
test baud (overlay edit + `west build --no-sysbuild`); \*4404's app still runs
the original 921600 build. `flash.sh` grew a `BAUD` env var for the verify
captures.

**2026-10-15 — \*6783 recovered, bench working again.** The user downgraded it
to the Jul-8-2025 image on a separate PC. Verified here three ways: the banner
(`compiled Jul  8 2025 10:15:34`, HW V1.00, S/N echoed 1057746783 — the
as-shipped image, = Greg's `ProdImg_250708`); the wire (the 180 s PASS row in
the matrix above — impossible on Jun-2026, whose ceiling is ~37 KB/s); and the
debug path (`nrfutil device reset --reset-kind RESET_HARD` rc=0, the nRF54L15
rebooted and printed its console banner, so SWD/CTRL-AP is intact with the older
probe firmware — the bench flashes as well as streams). No loss events in that
180 s window; they are episodic and the earlier 300 s run on this image saw 5,
so this says nothing about the original ~76 B defect either way. \*4404 was not
attached and is assumed still on Jun-2026; it is the board whose P1.04 carries
the FT232 tap, so it is the one to downgrade next.

### Fresh 600 s dual re-run with exact clock alignment (`reverify/`)

A second simultaneous 600 s capture on both boards (same A/B firmware, never
re-flashed, boards up since the ab run), this time with `# start_mono`
recording: the two captures started **10.2 µs apart**, so cross-board deltas
are physical, not launch skew.

- 16 events on each board, 0 corrupt frames, 100 % payload match, no reboots.
- **Pairing reproduces: 12 of 16 pair within ±0.5 s** (independence predicts
  ~0.4); real cross-board deltas −4 to −234 ms, median 68 ms, both signs.
- **The grid reproduces tightly**: best period 5.99 s with circular
  concentration r = 0.91-0.92 on each board *independently*; gaps are almost
  all ~6.0 s or ~12.0 s (two ticks) in this run.
- Phase split 8 vs 8 on both boards — granularity remains a null effect.
- Burst mean 26,658.14, modal 26,658 — production's number again.
- Bracketing read-gaps at every divergence: 20.08-20.13 ms (the designed
  cadence; whole-run max read-gap 76.7 ms) — losses again mid-flow.
- Event structure: 29 of 32 events single-run deletions; 1 foreign-bytes
  event (9 bytes matching no device output ever, inside a 50 B deletion), 1
  fragmented deletion (81 B deleted around a 9-byte in-span survivor), 1
  headless fragment (the deletion took the frame's head).

The two non-deletion textures from the existing captures both recur:
**foreign bytes inside the deletion window** (64 B in ab-4404 seq 407; 9 B in
rerun-6783 seq 19777 — searched against the frame, the whole capture, and the
device's entire regenerable output history: nothing) and **fragmented
deletions with in-span survivors** (ab-6783 seq 4736; rerun-6783 seq 20495;
plus the phase-run double-strikes). No event ever shows in-place bit damage
to content that arrived: 0 CRC failures across every capture.

## Results

**300 s soak at production's operating point** (2026-10-11, DK \*4404, corrected
geometry: burst mean 26,658.3 B, 88.0% duty, 3.04 bursts/s, 81.1 KB/s,
24,318,656 bytes captured):

| metric | value |
|--------|-------|
| loss events | **5 in 300 s — one per ~60 s** |
| bytes lost | 382 of 24,318,656 (0.0016%) |
| loss size | **68–86 B, mean 76 B** (the issue reports 64–80 B) |
| shape | one contiguous run deleted from a single frame, mid-frame (offsets 3.2–8.0 KB of ~13.3 KB), content realigning after it |
| corruption | none — 2,729/2,729 received frames CRC-valid and payload-exact |
| clustering | one lone event at 75 s, then four inside 22 s (195–217 s) |

That rate is exactly what #190 recorded from the *Bluetooth* firmware at this same
duty ("one per 60–100 s"), now reproduced with no BLE, no CS and no Rust
serializer in the path, and with a byte-exact location inside the frame.

An earlier 300 s soak ran ~99% duty / 91 KB/s (the gap bug above) and showed 18
events — but a repeat at that same hot duty showed **zero**, so run-to-run
variance from the episodic clustering dominates and two runs cannot establish a
duty dependence either way.

**Earlier 300 s soak, hot duty** (27,297,399 bytes captured, 91.0 KB/s):

| metric | value |
|--------|-------|
| frames | 2,031 valid, 0 corrupt, 20 unparsed (18 damaged + 2 capture boundaries) |
| content | 2,031/2,031 valid frames regenerate byte-exact |
| loss events | **18 in 300 s — one per ~17 s** (#190 recorded one per 60–100 s) |
| bytes lost | 1,516 of 27,297,399 (0.0056%) |
| loss size | **4–117 B, mean 84 B** — the 30 s run also caught a 4 B loss |
| shape | one contiguous run deleted from the **middle** of a single frame, content realigns after it |
| arrival | 0 slow arrivals — the device never stalls, the stream is continuous |

Two things the old harness could not see:

- **It is pure deletion, never corruption.** Not one received frame failed its
  CRC, and every surviving payload regenerates byte-exact. Bytes vanish; the
  bytes that arrive are always right. That rules out bit errors, baud drift and
  framing damage in one line.
- **The events are periodic, not random.** Gaps between consecutive events over
  the soak: `6.01, 6.00, 5.99, 6.01, 6.01, 7.03, 1.02, 1.02, 1.03, 1.02, 1.02,
  2.05, 3.95, 8.34, 1.46, 2.05, 8.78` s. A clean 6 s cadence, then a 1 s
  cadence, then irregular. Something with a period is doing this, not entropy.

## Controls already run

The `probe/` app established these on the same board, same capture tool:

- a single `uart_tx` of 39,950 B completes in 423 ms — exactly the wire time at
  921600 — and every size from 16 B up completes: the driver, the UARTE and the
  Interface MCU path carry full-size bursts;
- a TX buffer **rewritten immediately before each `uart_tx`** arrives
  byte-exact (800,000 × `0x42` over 25 s), so CPU-cache/EasyDMA incoherence on
  the nRF54L15 is *not* a candidate;
- sustained 87 KB/s through VCOM1 is byte-perfect over 1.5 MB with static
  payload, so the path is not dropping under load *per se*.

## Open threads

- ~~The two-receiver discriminator #190 asks for~~ **— RUN 2026-10-15, verdict
  below.** (`probe/` remains scratch: its two phases are the cache/rewrite
  control, not a test to keep.)

## Two-receiver discriminator (2026-10-15, night): the loss is in the Interface MCU, the SoC is exonerated at byte level

With both DKs restored to the Jul-8-2025 OB image, \*4404 moved to the
always-on aarch64 SBC (`rap0300002`, ssh mic@192.168.2.85) with its FT232 tap
on P1.04, and the discriminator finally ran: 600 s simultaneous dual capture —
VCOM1 plus the tap, same host, same monotonic clock. Captures archived in
`runs/discriminator-2026-10-15/` (the `.tsv` timing logs are gzip-compressed
throughout this tree — `gunzip` before re-analysing read gaps); analysis tool
`host/disc_compare.py` (selftest 4/4) walks both captures against the same
regenerated frames and compares coverage per lost byte range.

| receiver | events | bytes lost | notes |
|---|---|---|---|
| VCOM1 (IMCU bridge) | 48 | 71,284 | checker accounting via `sent_total` |
| FT232 tap (same TX pin) | 15 | 3,596 (net wire bytes, device-counter accounting) | kernel counters: `frame=4 overrun=84 buf_overrun=0` |

- **33 of the 48 VCOM events (40,776 B) were received 100% intact by the tap**
  — those bytes provably crossed the wire; the IMCU dropped them.
- **All 15 tap events pair with a VCOM event within ±0.5 s** (covering 15 VCOM
  events / 30,508 B): shared host-side stalls that make *both* receivers drop
  in-flight bytes — and the IMCU amplifies them ~8.5× (30,508 B vs 3,596 B).
- Byte-level union: **all but 239 B — 0.34% of the 71,284 B device-accounted loss —
  is covered by the tap's copy of the same frames.** (disc_compare's own resolved
  total is 85,227 B: it additionally counts ~14 KB of received-but-unattributable
  content as lost, all of it tap-covered too.) The 239 B sit inside the shared-stall events,
  in 17 small ranges (41–133 B) that the tap still covers *partially* (e.g. 35
  of 41 B). A device-side gap would be byte-identical in both captures; nothing
  is. Reverse comparison: of the tap's lost byte ranges in frame space (3,691 B per
  disc_compare vs 3,596 B net wire bytes per the device-counter accounting), 3,452 B
  are covered by VCOM — the tap's losses are receiver-side too.
- **New texture**: in this hot episode the IMCU's damage is *periodic* — ~2–205 B
  (mean ~60–75) deleted every ~390–620 B across the affected frames (walk-verified 387–488 B
  chunks separated by 58–90 B canonical holes, with short foreign-byte runs at
  the seams). The large "no-realignment" events in this same capture (19 of the 48
  are ≥ 2 KB) were these trains,
  under-resolved by the single-probe forensics.

**Verdict for #190: the sporadic COBS loss happens after the TX pin, inside
the Interface MCU's VCOM path, triggered by host-side activity.** The nRF54L15
and its UART are exonerated at byte level — no firmware-side retry or framing
change can recover bytes the chip demonstrably transmitted. Mitigation is
host-side (quiet host, external bridge, or a fixed IMCU image from
Nordic/SEGGER). Still open: the trigger actor (NetworkManager-correlated, ~6 s
grid) and the in-IMCU mechanism.

Throwaway diagnostic: delete with the rest of `tmp/`.

**Housekeeping 2026-10-16 (post-close):** the 2026-10-11 exploratory captures
(`ab-*`, `soak-*`, `dual-*`, `phase-*`, `batched-*`, `reverify/`,
`runs/hostprobe1`, `runs/hotcheck`, and the scratch tuning runs) were removed —
their findings are published in #190 and were re-verified by the independent
review, and the phenomenon is re-measurable with the kept harness on the kept
bench. Kept: the discriminator pair (`runs/discriminator-2026-10-15/`), the
Oct-14 FT232 matrix evidence (`runs/ft232-oct14/` — re-measuring it would mean
deliberately re-flashing the broken OB image), the restored-bench and 1 Mbps
witnesses, all tools and sources, and the OB firmware images. Capture `.tsv`
logs are gzip-compressed throughout this tree.
