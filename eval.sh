#!/bin/bash
# Evaluation harness mirroring the original Neuralink Compression Challenge eval.
# For each WAV file: encode -> decode -> verify byte-identical (lossless).
# Reports the aggregate compression ratio = sum(original) / sum(compressed).
#
# Usage: ./eval.sh [N]   (N = number of files to test; default = all)

set -u

ENCODE=./encode
DECODE=./decode
DATADIR=data
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

N="${1:-0}"

files=( "$DATADIR"/*.wav )
if [ "$N" -gt 0 ] 2>/dev/null; then
    files=( "${files[@]:0:$N}" )
fi

total_orig=0
total_comp=0
fail=0
count=0
enc_total_ms=0
dec_total_ms=0

for f in "${files[@]}"; do
    [ -e "$f" ] || continue
    comp="$TMP/c.nlc"
    dec="$TMP/d.wav"

    "$ENCODE" "$f" "$comp" 2>/dev/null
    "$DECODE" "$comp" "$dec" 2>/dev/null

    if ! cmp -s "$f" "$dec"; then
        echo "LOSSLESS FAIL: $f"
        fail=$((fail+1))
        continue
    fi

    o=$(stat -c%s "$f")
    c=$(stat -c%s "$comp")
    total_orig=$((total_orig + o))
    total_comp=$((total_comp + c))
    count=$((count+1))
done

echo "files tested:     $count"
echo "lossless failures: $fail"
echo "total original:   $total_orig bytes"
echo "total compressed: $total_comp bytes"
if [ "$total_comp" -gt 0 ]; then
    awk "BEGIN { printf \"compression ratio: %.4fx\\n\", $total_orig/$total_comp }"
fi
