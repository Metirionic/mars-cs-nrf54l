#!/usr/bin/env bash
# flash.sh -- flash a UART-stream image and PROVE it is the one running.
#
# `nrfutil device program` exits 0 and is silent, and it leaves the core
# halted -- a dead console then looks exactly like a hung firmware. It has also
# been seen to leave the *previous* image running. So every flash is verified on
# the wire before any measurement: hard-reset the core, listen on the console
# for a string only this image prints, and retry until it appears.
#
# Keep the expected string SHORT and EARLY in the line: the console leg drops
# the tail of long lines (#190's own console-loss finding), so an expectation
# anchored at the end of a long banner reports a false "flash did not take".
#
# Usage: flash.sh <jlink-sn> <hex> <expect-on-console> [tries]
# Env:   CONSOLE_PORT (default /dev/ttyACM4 -- VCOM0 of DK *4404)
set -uo pipefail

SN="$1"; HEX="$2"; EXPECT="$3"; TRIES="${4:-4}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CONSOLE_PORT="${CONSOLE_PORT:-/dev/ttyACM0}"  # *4404 VCOM0 (mapping moves: check by-id)
STREAM_PORT="${STREAM_PORT:-/dev/ttyACM1}"  # *4404 VCOM1 (mapping moves: check by-id)
BAUD="${BAUD:-921600}"
LOG="${TMPDIR:-/tmp}/uart-stream-flash.log"

for attempt in $(seq 1 "$TRIES"); do
  nrfutil device program --firmware "$HEX" --serial-number "$SN" \
    --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_SYSTEM \
    >"$LOG" 2>&1
  rc=$?
  # program leaves the core halted, so the image never runs without a reset.
  # RESET_HARD rides the CTRL-AP: on SecDom devices (nRF54L15) RESET_SYSTEM is
  # secure-core-only and a plain `nrfutil device reset` default has been seen
  # not to boot the app core at all.
  nrfutil device reset --serial-number "$SN" --reset-kind RESET_HARD >/dev/null 2>&1 \
    || nrfutil device reset --serial-number "$SN" >/dev/null 2>&1
  sleep 0.4

  # Two independent witnesses, because either leg can be dead on its own:
  # the console (VCOM0) stalls in stretches -- that is #190's own console-loss
  # finding -- and a streaming image (VCOM1) is proven by frames that parse.
  # Remove the witness files first: a capture that fails (wrong port, port
  # renumbered) writes nothing, and `check` then reads the PREVIOUS run's file
  # and happily reports frames -- a stale-file verification. Cost the 1 Mbps
  # experiment one false "verified" before this line existed.
  rm -f "${TMPDIR:-/tmp}/uart-stream-verify.bin" "${TMPDIR:-/tmp}/uart-stream-verify-stream.bin"
  python3 "$HERE/host/stream_check.py" capture "$CONSOLE_PORT" --baud "$BAUD" \
    --out "${TMPDIR:-/tmp}/uart-stream-verify.bin" --duration 3 >/dev/null 2>&1
  if tr -d '\000' < "${TMPDIR:-/tmp}/uart-stream-verify.bin" | grep -aq -- "$EXPECT"; then
    echo "flash: verified on attempt $attempt -- console shows '$EXPECT'"
    exit 0
  fi

  python3 "$HERE/host/stream_check.py" capture "$STREAM_PORT" --baud "$BAUD" \
    --out "${TMPDIR:-/tmp}/uart-stream-verify-stream.bin" --duration 2 >/dev/null 2>&1
  python3 "$HERE/host/stream_check.py" check \
    "${TMPDIR:-/tmp}/uart-stream-verify-stream.bin" >/dev/null 2>&1
  # exit 0 (clean) or 1 (anomalies) both mean real frames arrived; 2 means none.
  if [ $? -ne 2 ]; then
    echo "flash: verified on attempt $attempt -- stream frames parse on $STREAM_PORT"
    exit 0
  fi
  echo "flash: attempt $attempt (nrfutil rc=$rc) -- '$EXPECT' absent from the console; retrying" >&2
  cat "$LOG" >&2
  sleep 1
done

echo "flash: FAILED after $TRIES attempts -- the board is not running $HEX" >&2
exit 1
