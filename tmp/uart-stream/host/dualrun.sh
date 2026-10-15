#!/usr/bin/env bash
# dualrun.sh -- one labelled simultaneous dual capture + full analysis.
#
# Usage: dualrun.sh <label> [duration_s]
#
# Captures both DKs' VCOM1 simultaneously (cadence 20, the sane-reader mode),
# then runs check + exact-alignment pairing + per-board summaries. Results land
# in tmp/uart-stream/runs/<label>/; the pairing verdict prints to stdout.
# Port mapping is NOT stable across reboots -- re-derive with nrfutil device
# list if in doubt (4404 = VCOM1 on ttyACM5, 6783 = VCOM1 on ttyACM3, as of
# 2026-10-11).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
LABEL="${1:?label required}"
DUR="${2:-600}"
OUT="$HERE/../runs/$LABEL"
mkdir -p "$OUT"

PORT_A="${PORT_A:-/dev/ttyACM5}"   # DK *4404 VCOM1
PORT_B="${PORT_B:-/dev/ttyACM3}"   # DK *6783 VCOM1

echo "== dualrun $LABEL: ${DUR}s on $PORT_A + $PORT_B"
python3 "$HERE/stream_check.py" capture "$PORT_A" --out "$OUT/A.bin" --duration "$DUR" --cadence 20 &
pid_a=$!
python3 "$HERE/stream_check.py" capture "$PORT_B" --out "$OUT/B.bin" --duration "$DUR" --cadence 20 &
pid_b=$!
wait "$pid_a"; wait "$pid_b"

python3 "$HERE/stream_check.py" check "$OUT/A.bin" --json > "$OUT/A.json"
python3 "$HERE/stream_check.py" check "$OUT/B.bin" --json > "$OUT/B.json"
python3 "$HERE/pair_analysis.py" abs-pairing "$OUT/A.json" "$OUT/A.bin.tsv" "$OUT/B.json" "$OUT/B.bin.tsv" | tee "$OUT/pairing.txt"
for b in A B; do
  echo "== board $b" | tee -a "$OUT/pairing.txt"
  python3 - "$OUT/$b.json" <<'EOF' | tee -a "$OUT/pairing.txt"
import json, sys
rep = json.load(open(sys.argv[1]))
losses = rep["anomalies"]
print(f"events={len(losses)} lost={rep['bytes_lost']} corrupt={rep['frames_corrupt']} "
      f"match={rep['payloads_matching_regeneration']}/{rep['frames_valid']} "
      f"reboots={len(rep.get('device_reboots', []))}")
print("times:", [round(l["at_s"], 2) for l in losses])
print("sizes:", [l["bytes_lost"] for l in losses])
EOF
done
echo "== full reports: $OUT/"