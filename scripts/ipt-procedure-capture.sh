#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# IPT procedure-capture bench harness (regression harness for #173).
#
# Captures both nRF54L15 DK consoles for a fixed window while the initiator
# runs the IPT pair, and asserts on the empty-procedure signature:
#   - initiator "IPT procedure produced no step data" warnings  (want 0)
#   - reflector "CS subevent aborted" lines                     (want 0)
#   - "CS liveness watchdog" recoveries on either console       (want 0)
#   - serialized events on "Run serialization"                  (≈ wire rate)
#   - "Serialize backlog: dropped" warnings                     (the #173
#     backpressure valve — visible only above the wire-rate cadence)
#
# Root cause the harness locks down (#173): serialize + UART TX must run off
# the BT RX thread (cs_initiator_consume_pending_event). With the pre-#173
# inline serialize, ~70 % of completed procedures at procedure_interval = 1
# arrived empty (the SDC aborted their subevents because the RX thread was
# blocked in the 289 ms UART TX), starving the #116 liveness watchdog. The
# same ~289 ms per event at 921600 baud is the wire-rate ceiling (~3.5
# events/s), the cadence above which backlog drops start to appear. The
# numbers are canonical in docs/architecture.md.
#
# Requirements: both DKs attached and flashed with the IPT pair
# (nrf54l15dk_cent_a1_4_ipt + nrf54l15dk_peri_a1_4_ipt); the boards'
# DEBUG OUT VCOMs reachable under /dev/serial/by-id. Hardware-only —
# there is no CI seam for this timing behaviour.
#
# Usage: scripts/ipt-procedure-capture.sh [duration_seconds] [tag]
# Exit 0 = all assertions pass, 1 = red (or capture empty).
set -u
set -o pipefail

DUR=${1:-30}
TAG=${2:-capture}
OUT=/tmp/ipt-capture
mkdir -p "$OUT"

# Bench serials as they appear in /dev/serial/by-id (leading zeros included;
# nrfutil device list prints them trimmed — check `ls /dev/serial/by-id/`).
INIT_SN=${INIT_SN:-001057744404}
REFL_SN=${REFL_SN:-001057746783}
INIT_CON="/dev/serial/by-id/usb-SEGGER_J-Link_${INIT_SN}-if00"
REFL_CON="/dev/serial/by-id/usb-SEGGER_J-Link_${REFL_SN}-if00"

for dev in "$INIT_CON" "$REFL_CON"; do
    if [ ! -e "$dev" ]; then
        echo "MISSING console device: $dev (re-derive with 'nrfutil device list')" >&2
        exit 1
    fi
    stty -F "$dev" 921600 raw -echo
done

timeout "$DUR" cat "$INIT_CON" > "$OUT/init_$TAG.log" &
P1=$!
timeout "$DUR" cat "$REFL_CON" > "$OUT/refl_$TAG.log" &
P2=$!
# Collect the two exit statuses separately — `wait A B` only reports B's.
wait "$P1"; INIT_ST=$?
wait "$P2"; REFL_ST=$?

# A dead console must not degrade into a misleading RED below. `timeout`
# exits 124 when the window expires (expected) and 0 when the stream closed
# early; anything else means that console capture failed.
capture_ok() {
    local side=$1 status=$2
    case "$status" in
        124 | 0) return 0 ;;
        *)
            echo "CAPTURE FAILED: $side console capture exited $status (want 124 = window expired)" >&2
            exit 1
            ;;
    esac
}
capture_ok initiator "$INIT_ST"
capture_ok reflector "$REFL_ST"

python3 - "$OUT/init_$TAG.log" "$OUT/refl_$TAG.log" "$TAG" <<'PY'
import re
import sys

init_log, refl_log, tag = sys.argv[1], sys.argv[2], sys.argv[3]

init = open(init_log, errors="replace").read()
refl = open(refl_log, errors="replace").read()

serialized = init.count("Run serialization")
empty = init.count("IPT procedure produced no step data")
backlog = len(re.findall(r"Serialize backlog: dropped (\d+) procedures", init))
# Console-visible literal (common/cs_watchdog.c:29). The bare "CS watchdog"
# banner lives only on the COBS wire (serialize_send_log_message), which this
# harness does not capture. Both boards run the watchdog.
watchdog = init.count("CS liveness watchdog")
refl_watchdog = refl.count("CS liveness watchdog")
refl_aborts = refl.count("CS subevent aborted")

# Last serialized procedure counter bounds the scheduled cadence.
last_proc = int(counters[-1]) if (counters := re.findall(r"Run serialization for procedure (\d+)", init)) else -1

print(f"[{tag}] serialized={serialized} empty_warnings={empty} backlog_reports={backlog} "
      f"watchdog={watchdog} refl_watchdog={refl_watchdog} reflector_aborts={refl_aborts} "
      f"last_proc_counter={last_proc}")

fail = []
if serialized == 0:
    fail.append("no serializations captured (pair not running?)")
if empty:
    fail.append(f"{empty} empty-procedure warnings (SDC aborted subevents — RX thread blocked?)")
if refl_aborts:
    fail.append(f"{refl_aborts} reflector aborts")
if watchdog:
    fail.append(f"{watchdog} initiator watchdog recoveries")
if refl_watchdog:
    fail.append(f"{refl_watchdog} reflector watchdog recoveries")
if fail:
    print("RED: " + "; ".join(fail))
    sys.exit(1)
print("GREEN")
PY
