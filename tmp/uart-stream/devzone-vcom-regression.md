# nRF54L15 DK: newest J-Link OB firmware (Jun-2026, J-Link V9.84 pack) breaks VCOM UART reception at 921600/460800 — 115200 is byte-perfect

## Summary

On the nRF54L15 DK (PCA10156, Interface MCU = nRF5340, probe identifies as "J-Link
OB-nRF5340-NordicSemi"), the Interface-MCU firmware **compiled Jun 11 2026** (bundled with
J-Link software V9.84, released 2026-10-07) cannot receive sustained high-rate UART data
on VCOM: at **921600** and **460800** baud, long frames arrive damaged mid-frame and the
delivered data is unusable. At **115200** the same path is **byte-perfect**. The firmware
the DKs shipped with (**compiled Jul 8 2025**) delivered the same 921600 stream 99.998 %
intact in our measurements. The regression is entirely in the probe firmware: same app,
same board, same host — only the OB image differs.

We hit this while characterizing a separate, much smaller loss issue (sporadic ~76-byte
deletions, one per ~60 s, on the shipped Jul-2025 firmware — tracked locally). To A/B
test firmware versions we flashed the new image; the difference is not subtle.

## Test setup

- 2× nRF54L15 DK (PCA10156, hardware V1.00), target app on the nRF54L15.
- A BLE-free Zephyr test app streams COBS frames on `uart20` (P1.04/P1.05 → VCOM1) at
  **921600 8N1**, mirroring our production transfer shape: 26,658-byte bursts at ~88 %
  duty (~81 KB/s), each burst = two 13.3 KB event frames + a 26-byte status frame.
- Every frame carries a sequence number, a CRC32, and the device's own **cumulative
  wire-byte counter** — so the host can attribute every missing byte exactly and detect
  in-place corruption (CRC failures) separately from drops.
- Host: Ubuntu (kernel 6.8), plain `cdc_acm` (no pyserial; raw termios, hw flow control
  off). No X. Both VCOMs open-able normally; `nrfutil device list` mapping re-derived
  after each probe re-enumeration.

Firmware update method (for reproduction):

```
nrfutil device x-update-debug-probe-firmware --serial-number <SN> \
    --jlink-dll <path-to-JLink_V9.84>/libjlinkarm.so
```

nrfutil prints `Failed to update the firmware of the debug probe: Unspecified error
from J-Link (Generic)` after `Could not read J-Link capabilities` — **the update still
succeeds**; verify with the J-Link Commander banner (`Firmware: J-Link
OB-nRF5340-NordicSemi compiled Jun 11 2026 13:12:28`).

⚠️ Note: in our run this command **updated both attached J-Link OBs even though one
serial number was given** (the probe re-enumerates mid-update and the DLL re-attaches).
If you have two probes connected, disconnect the one you do not want to touch, and
verify afterwards against the *echoed S/N*, not the banner alone.

## Measurements (same target app, same board, same host; only OB firmware and baud differ)

| OB firmware | stream baud | delivered | usable frames | verdict |
|---|---|---|---|---|
| Jul 8 2025 (as shipped) | 921600 | ~81 KB/s, 99.998 % intact | all | works (sporadic ~76 B deletions, ~1/60 s — separate issue) |
| Jun 11 2026 (V9.84 pack) | 921600 | **~37 KB/s (~45 %)** | **0 of ~122 event frames parse in 20 s**; only the 26-byte status frames survive | broken |
| Jun 11 2026 | 460800 | ~26 KB/s of ~43 KB/s | 10 of 147 frames / 30 s | broken |
| Jun 11 2026 | 115200 | full rate | **18/18 frames, 100 % payload match** | **byte-perfect** |

Damage character at 921600/460800 on Jun-11-2026:

- **Not** whole-stream garbage: frame *heads* keep arriving (≈87 frame-start sequences in
  a 20 s capture) and the short status frames that survive decode byte-exact — including
  their CRC32s. The 13.3 KB event frames never decode.
- The delivered rate scales with the host-requested baud (115200→4.5, 230400→12.6,
  921600→36.9 KB/s), so the CDC baud configuration plumbing works; sustained
  high-rate reception does not.
- The target app is provably healthy under all conditions: the surviving status frames
  carry the device's cumulative byte counter, which advances by exactly 26,659 per burst
  throughout — the nRF54L15 emitted every byte; the loss/damage happens between the
  target's TX pin and the host.

The shipped Jul-2025 firmware, for comparison, delivered the same 921600 stream with
0 CRC failures across every capture we have (only sporadic small deletions, never
in-place corruption).

## Questions

1. Is this a known regression in the OB-nRF5340 VCOM reception path between the
   Jul-2025 and Jun-2026 builds?
2. Is a fixed OB image planned? (We cannot move back: the V7.92-era bundled image
   cannot be flashed onto the updated probe — the old DLL refuses the downgrade and
   the 2023-era JLinkExe crashes on current glibc — so the Jun-2026 bootloader
   appears to require current tooling.)
3. Interim guidance for anyone streaming > 115200 through VCOM on nRF54 DKs: is the
   recommendation simply "do not update the probe firmware", or is there a supported
   route we missed?

Happy to share the capture files, the test app, and the byte-exact analyzer (a
selftest-proven Python tool) — everything reproduces from a stock DK in ~10 minutes.

## Environment

- nRF54L15 DK ×2 (PCA10156, HW V1.00), IMCU firmware as listed above
- J-Link software V9.84 (Linux, x86_64) for the update; V7.92m also present
- nrfutil device 8.2.0 (`x-update-debug-probe-firmware`)
- Host: Ubuntu 24.04-era (kernel 6.8), cdc_acm
- Target: Zephyr on nRF54L15 (NCS v3.4.0), uart20 @ 921600 8N1


## Addendum — sent 2026-10-15

Three updates since the original report. (This also supersedes the "we cannot move
back" wording in Question 2 — the older image could be restored, from a machine whose
J-Link tooling is newer than this bench's V7.92m.)

**1. 1 Mbps is affected too, and the limit is throughput, not baud.** With the
Jun-11-2026 image, VCOM1 delivers **~37 KB/s whatever the configured rate**: 37.2 KB/s
at 921600 and 36.8 KB/s at 1 Mbps, while the offered rate rose from 81 to 85 KB/s.
Long frames arrive damaged mid-frame; only the 26-byte status frames survive; 115200
(~11 KB/s offered) is byte-perfect because it fits under the ceiling. This also
explains the 460800 result. **Ask:** can you confirm the VCOM's documented *throughput*
limit? "Up to 1 Mbps" describes the baud the debugger accepts, not what its VCOM
carries.

**2. Two receivers on the same wire put the loss inside the debugger.** A passive FT232
tap on the nRF's TX pin (P1.04), captured simultaneously with VCOM1, received **100 % of
the bytes at full rate** with zero driver drops (`frame=0 overrun=0 buf_overrun=0`),
while VCOM1 delivered 46 % of them. The SoC's output is intact.

**3. The regression is dated.** `compiled Jul  8 2025 10:15:34` — the image the DKs ship
with — delivers 81 KB/s byte-exact; `compiled Jun 11 2026 13:12:28` is the broken one,
and it is what the J-Link V9.84 **and** V9.86 packs bundle (byte-identical file). We
restored a board to the Jul-2025 image as a workaround, which confirms the firmware image
is the only variable. The original asks therefore narrow to: **is this a known regression
between those two dated images, and is a fixed image planned?**