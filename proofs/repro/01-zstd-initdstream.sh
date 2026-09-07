#!/bin/sh
# FINDING: BIT_initDStream forms a pointer past the end of the caller's object,
# reachable from the public ZSTD_decompressBlock.
# Full write-up: ../zstd/findings/FINDING-initdstream-limitptr.md
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ZSTD=${ZSTD:-$HOME/facebook/zstd}
[ -d "$ZSTD" ] || { echo "SKIP 01-zstd-initdstream: no zstd tree at $ZSTD"; exit 0; }

W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
echo "== zstd BIT_initDStream: pointer past the caller's object =="

# The probe returns early after the four stream inits. Removing later code is an
# under-approximation: it cannot manufacture a counterexample, and it takes this
# from a 2400s timeout to seconds.
cc -E -DNDEBUG -DZSTD_REACH_PROBE -I "$ZSTD/lib/common" -I "$ZSTD/lib" \
   -I "$ZSTD/lib/decompress" "$HERE/../zstd/harnesses/harness_huf4x_subrange.c" \
   -o "$W/h.i" 2>/dev/null || { echo "  (needs the ZSTD_REACH_PROBE patch; see the finding)"; exit 0; }
sed -e 's/__builtin_memcpy/memcpy/g' -e 's/__builtin_memmove/memmove/g' "$W/h.i" > "$W/h2.i"
{ echo 'void __CPROVER_assume(int);'
  echo 'void *__CPROVER_allocate(unsigned long, int);'
  cat "$W/h2.i"; } > "$W/h3.i"
goto-cc "$W/h3.i" -o "$W/h.goto" 2>/dev/null

# --pointer-overflow-check is load-bearing: --pointer-check alone reports this
# clean, which is why the defect survived.
"$HERE/../solve.sh" -t 900 "$W/h.goto" --function harness \
    --pointer-overflow-check --object-bits 12 --unwind 2 --no-unwinding-assertions \
  2>&1 | grep -E "pointer_arithmetic.*FAILURE|^\*\* [0-9]+ of|VERIFICATION|solved by"
