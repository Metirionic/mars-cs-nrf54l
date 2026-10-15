#!/usr/bin/env bash
# stall-capture.sh — reproduce host-backpressure drops on the DK COBS UART.
#
# Reads the ranging stream raw, then stops draining it for STALL_LEN seconds
# (SIGSTOP on the reader simulates a host that is descheduled / blocked / slow),
# resumes, drains, and validates the capture for COBS frame anomalies.
#
# Usage: stall-capture.sh <port> <total_s> <stall_at_s> <stall_len_s> <out.bin>
#   stall_len_s = 0 -> control run, no stall
set -uo pipefail

PORT="$1"; TOTAL="$2"; STALL_AT="$3"; STALL_LEN="$4"; OUT="$5"
HERE="$(cd "$(dirname "$0")" && pwd)"

stty -F "$PORT" 921600 raw -echo -hupcl
: > "$OUT"

cat "$PORT" > "$OUT" &
PID=$!

sleep "$STALL_AT"
if [ "$STALL_LEN" != "0" ]; then
  kill -STOP "$PID" 2>/dev/null
  sleep "$STALL_LEN"
  kill -CONT "$PID" 2>/dev/null
fi
sleep "$(( TOTAL - STALL_AT - STALL_LEN > 0 ? TOTAL - STALL_AT - STALL_LEN : 0 ))" 2>/dev/null || \
  sleep "$TOTAL"

kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null
echo "port=$PORT total=${TOTAL}s stall=${STALL_LEN}s@${STALL_AT}s bytes=$(stat -c%s "$OUT")"
python3 "$HERE/cobs_check.py" "$OUT"
