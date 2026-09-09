#!/bin/sh
# FINDING: BIT_initDStream forms a pointer past the end of the caller's object.
#   bitD->limitPtr = bitD->start + sizeof(bitD->bitContainer);
# runs before any check that the buffer is that long. C 6.5.6p8 allows pointer
# arithmetic only within an object and one past its end, so a buffer shorter
# than a bitContainer makes this UB -- with no dereference, which is why ASan
# and every fuzzer miss it.
#
# The contract is the whole reproduction: no harness, no __CPROVER_assume.
# Full write-up: ../zstd/findings/FINDING-initdstream-limitptr.md
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ZSTD=${ZSTD:-$HOME/facebook/zstd}
[ -d "$ZSTD" ] || { echo "SKIP 01-zstd-initdstream: no zstd tree at $ZSTD"; exit 0; }
H="$ZSTD/lib/common/bitstream.h"
grep -q "c_pre (c_fresh(srcBuffer" "$H" 2>/dev/null || {
  echo "SKIP 01-zstd-initdstream: bitstream.h is not annotated"
  echo "  apply ../zstd/patches/annotate-initdstream.patch"; exit 0; }

echo "== zstd BIT_initDStream: pointer past the caller's object =="
echo "   contract under test:"
sed -n '/^MEM_STATIC size_t BIT_initDStream/,/^{$/p' "$H" |
  grep -E "^\s+c_pre " | sed 's/^/     /'

# fresh() rather than readable(): readable is a LOWER bound, so CBMC may give
# the object slack past srcSize -- and that slack is exactly what hides this.
#
# srcSize == 8 is the control. A buffer as long as the bitContainer makes
# start + 8 a legal one-past-the-end pointer, so it must verify clean. A red
# that does not go away there is not a red about the buffer length.
W=$(mktemp -d); trap 'rm -rf "$W"; cp "$W.h" "$H" 2>/dev/null' EXIT
cp "$H" "$W.h"
for N in 4 8; do
  cp "$W.h" "$H"
  perl -0pi -e "s/c_pre \(srcSize == \d+\)/c_pre (srcSize == $N)/s" "$H"
  printf '   srcSize == %-3s ' "$N"
  UNWIND=8 DEADLINE=${DEADLINE:-900} \
  CPPFLAGS="-U__ARM_NEON -DZSTD_NO_INTRINSICS" "$HERE/../verify-contract.sh" \
      BIT_initDStream "$ZSTD/lib/decompress/huf_decompress.c" \
      -I "$ZSTD/lib/common" -I "$ZSTD/lib" -I "$ZSTD/lib/decompress" 2>&1 |
    grep -E "^\*\* [0-9]+ of|solved by" | tr '\n' ' '
  echo
done
cp "$W.h" "$H"

cat <<'NOTE'
   --pointer-overflow-check is load-bearing: --pointer-check alone reports this
   clean, which is why the defect survived in mature, heavily fuzzed code.

   Reachability -- that a real compressed stream drives this with a short buffer
   -- is a whole-program question, not a function contract, and is recorded
   separately in the finding.
NOTE
