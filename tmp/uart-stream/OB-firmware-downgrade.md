# DK J-Link OB firmware downgrade — restores VCOM streaming (issue #190)

## Symptom

VCOM1 (the DK's "Serial Port 1") caps at **~37 KB/s regardless of the configured baud**;
long frames arrive damaged mid-frame (only short frames survive); 115200 looks perfect
because it fits under the ceiling. The nRF firmware is **not** the cause — an external
FT232 on the same TX pin sees 100 % of the bytes. 921600, 460800 and 1 Mbps all break.

## 1. Confirm the culprit

Read the probe's banner (keep `-SelectEmuBySN <SN>` and check the echoed S/N):

```
JLinkExe -SelectEmuBySN <SN> -CommanderScript <file containing "ShowHWStatus">
```

- **broken:** `compiled Jun 11 2026 13:12:28` — what the J-Link **V9.84 and V9.86 packs**
  both bundle (byte-identical image, md5 `b2e3643dddf630721c5ed6068c6bfef9`)
- **good:** `compiled Jul  8 2025 10:15:34` — as shipped on the DK
  (= `JLink_OB_nRF5340_NordicSemi_ProdImg_250708.hex`)

## 2. Get the Jul-2025 image

Use the hex if you have it. Otherwise take it from an older SEGGER pack:
`segger.com/downloads/jlink` has a version picker (back to V5.00), but the download only
works through a **real browser** with the licence checkbox ticked — `curl`/`HEAD` gets
HTTP 405 + an AWS WAF captcha.

**Which pack version carries it is now known** (bisected 2026-10-15): **V8.52
(released 2025-07-16) is the first release carrying `compiled Jul  8 2025 10:15:34`**,
md5 `c61ae847def9f306faaeef0248269945`, 81,437 B; V8.54–V8.58 carry the same file.
Do *not* use V8.48/V8.50 (they still carry the Apr-1-2025 build) or V9.84/V9.86 (the
broken Jun-2026 one). Always check the extracted file rather than trusting the version:

Known images, by `strings -a <file> | grep compiled`: Jul 2 2021 (65,231 B) ·
Oct 17 2023 (68,531 B) · Apr 1 2025 (81,440 B) · **Jul 8 2025 (81,437 B — the as-shipped
good one)** · Jun 11 2026 (85,390 B — the ~37 KB/s VCOM regression).

Tip: the version→date map is inside any pack, at `Doc/ReleaseNotes/ReleaseNotes.html`
(`<h2>Version V### (YYYY-MM-DD)</h2>`), so candidate versions can be chosen offline.
Build-to-release lag is ~8–9 days (V7.92m: 9, V8.52: 8).


```
dpkg-deb --fsys-tarfile JLink_Linux_V###_x86_64.deb \
  | tar -xO -f - ./opt/SEGGER/JLink_V###/Firmwares/JLink_OB_nRF5340_NordicSemi.bin > img.bin
strings -a img.bin | grep compiled
```

On any OS the file lives at `<J-Link install>/Firmwares/JLink_OB_nRF5340_NordicSemi.bin`.

## 3. Flash it — `exec InvalidateFW` from a desired-version Commander (proven 2026-10-15)

`nrfutil device x-update-debug-probe-firmware` **cannot downgrade**: the DLL compares
versions and silently skips the write — `versionBeforeUpdate == versionAfterUpdate`, rc=0,
wire unchanged. It is an upward-update path only. (`x-boardcontroller-program` is not a
route for this DK either: no Nordic board-controller artifact exists for PCA10156 — that
flow is for nRF9161-family IMCUs — and SEGGER's `.bin` fails its validation cleanly with
"Failed to read image metadata", writing nothing.)

The documented downgrade (SEGGER KB "J-Link - Firmware") is `exec InvalidateFW` from a
J-Link Commander **of the desired version** — and "desired version" in practice means:
any Commander whose sibling `Firmwares/` holds the image you want. Stage one without
touching the installed pack (proven on aarch64, headless):

```bash
mkdir -p ~/jlink-downgrade/JLink_V986/Firmwares
cp -L <installed pack>/libjlinkarm.so  ~/jlink-downgrade/JLink_V986/libjlinkarm.so
ln -sf  libjlinkarm.so                 ~/jlink-downgrade/JLink_V986/libjlinkarm.so.9
cp    <installed pack>/JLinkExe        ~/jlink-downgrade/JLink_V986/JLinkExe
cp    <the Jul-2025 image>             ~/jlink-downgrade/JLink_V986/Firmwares/JLink_OB_nRF5340_NordicSemi.bin

printf "exec InvalidateFW\nexit\n" > /tmp/jl-inv.jlink
cd ~/jlink-downgrade/JLink_V986
LD_LIBRARY_PATH=$PWD timeout 150 ./JLinkExe -SelectEmuBySN <SN> -CommanderScript /tmp/jl-inv.jlink
```

The command invalidates the probe's firmware, then **self-heals from that `Firmwares/`
directory** — the output names the build it writes, so read it:

```
Updating firmware:  J-Link OB-nRF5340-NordicSemi compiled JUL  8 2025 10:15:34   <- what it writes
Replacing firmware: J-Link OB-nRF5340-NordicSemi compiled Jun 11 2026 13:12:28   <- what was there
Waiting for new firmware to boot
New firmware booted successfully
```

- **Do not run this from the installed (newer) Commander** — its self-heal restores the
  newer image instead (observed: `Replacing firmware: … compiled Jun 11 2026`).
- **Afterwards, only connect with matching-or-older tooling.** A DLL whose bundled image
  is newer than the probe's will auto-replace it on connect — headless, automatically
  (SEGGER KB; **observed 2026-10-15 ~22:00 UTC on our SBC**: the downgraded probe was
  silently re-upgraded between two logged moments — Jul-2025 verified at 21:37Z,
  Jun-2026 found at 22:01Z — by J-Link tooling that left no nrfutil log entry, e.g. the
  system JLinkExe/Configurator or an nrfutil run as root; no prompt, no warning) —
  silently undoing the downgrade. On a box with a newer pack installed, pass
  `--jlink-dll ~/jlink-downgrade/JLink_V986/libjlinkarm.so` to every nrfutil
  command, and use the staged Commander instead of the `JLinkExe` on PATH.

## 4. Verify — all three; the banner alone is not proof

1. **Banner**: `compiled Jul  8 2025 10:15:34`, echoed S/N matches the target probe —
   read it with the **staged** Commander (`LD_LIBRARY_PATH=… ./JLinkExe -SelectEmuBySN …`),
   never with a newer installed one (it would auto-re-upgrade the probe).
2. **Wire, content-checked — not just byte-counted**: a sustained stream (≥10 s at
   921600) must parse as full frames with **0 damaged frames and 100 % payload match**
   (~81 KB/s, 3 frames/burst). A short `wc -c` window is *not* evidence: right after a
   probe reboot the delivered byte rate can look full while the stream is already
   damaged (observed: 81 KB/s at +6 s on the Jun-2026 image, while the 10 s
   content-checked capture showed 37 KB/s with 24 loss events).
   Harness (if present): `python3 host/stream_check.py capture <port> --baud 921600
   --out run.bin --duration 10` then `... check run.bin` → expect `VERDICT: PASS`.
3. **Debug path**: `nrfutil device reset --serial-number <SN> --reset-kind RESET_HARD
   --jlink-dll ~/jlink-downgrade/JLink_V986/libjlinkarm.so` → rc 0, then the target
   reboots and prints its console banner → SWD/CTRL-AP still works with the older probe
   firmware (the bench flashes as well as streams). Always pass the staged DLL here too.

## 5. Afterwards

Do **not** let SEGGER tooling update that probe again — it will push the Jun-2026 image
straight back. And no baud change, host change or target-firmware change will help: the
loss is inside the debugger's VCOM path.
