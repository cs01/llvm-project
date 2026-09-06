#!/bin/sh
# Prove ZSTD_wildcopy memory-safe for every length, with the contracts written
# in this fork's grammar rather than in CBMC's macros.
#
#   patches/annotate-wildcopy-our-grammar.patch  annotates real zstd with
#     assigns / loop_invariant / decreases
#   this script                                  lowers, compiles, proves
#
# Usage:  ZSTD=~/git/zstd CLANG=../../build/bin/clang ./run-wildcopy-from-grammar.sh
#
# Expected:  ** 0 of 208 failed (1 iterations) / VERIFICATION SUCCESSFUL
set -e
ZSTD=${ZSTD:?set ZSTD to a zstd checkout with the patch applied}

# CBMC 6 or newer. Ubuntu 24.04 ships 5.95, and loop-contract handling changed
# substantially across that major: COST.md's recorded times are all 6.11. Get a
# release .deb from github.com/diffblue/cbmc/releases rather than from apt.
CBMC_MAJOR=$(cbmc --version | cut -d. -f1)
[ "${CBMC_MAJOR:-0}" -ge 6 ] || {
  echo "cbmc $(cbmc --version) is too old; this needs 6.x" >&2; exit 1; }

CLANG=${CLANG:-clang}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)

# 1. Preprocess with the SYSTEM compiler, not clang. goto-cc cannot parse the
#    _Float128 declarations clang's glibc expansion leaves behind.
cc -E -DNDEBUG -DZSTD_CONTRACTS -I "$ZSTD/lib/common" -I "$ZSTD/lib" \
   "$HERE/harnesses/harness_wildcopy.c" -o "$WORK/h.i"

# 2. CBMC models the plain names, not the builtins Apple/glibc route through.
sed -e 's/__builtin_memcpy/memcpy/g' -e 's/__builtin_memmove/memmove/g' \
    "$WORK/h.i" > "$WORK/h2.i"

# 3. The harness calls CBMC builtins that have no declaration in a normal TU.
{ echo 'void __CPROVER_assume(int);'
  echo 'void *__CPROVER_allocate(unsigned long, int);'
  cat "$WORK/h2.i"; } > "$WORK/h3.i"

# 4. Lower our grammar to CBMC's. Errors from gcc's mmintrin.h are expected and
#    harmless: clang rejects gcc's MMX vector builtins, which zstd pulls in
#    through compiler.h and wildcopy does not use. The rewrite still emits every
#    contract, which step 5 checks.
"$CLANG" -cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit \
    "$WORK/h3.i" > "$WORK/wc.c" 2>"$WORK/rewrite.log" || true

CLAUSES=$(grep -c '__CPROVER_loop_invariant\|__CPROVER_assigns\|__CPROVER_decreases' "$WORK/wc.c")
[ "$CLAUSES" -eq 7 ] || { echo "expected 7 lowered clauses, got $CLAUSES"; exit 1; }
echo "lowered $CLAUSES contract clauses from the grammar:"
grep '__CPROVER_loop_invariant\|__CPROVER_assigns\|__CPROVER_decreases' "$WORK/wc.c" | sed 's/^ */  /'

# 5. Prove. No --unwind: the loop contract replaces the bound with induction.
goto-cc "$WORK/wc.c" -o "$WORK/wc.goto"
goto-instrument --apply-loop-contracts "$WORK/wc.goto" "$WORK/wci.goto"
# --bounds-check --pointer-check only. Adding --pointer-overflow-check to this
# harness did not finish in 40 minutes; UNBOUNDED.md's recorded run does not use
# it and returns in one iteration.
cbmc "$WORK/wci.goto" --function harness --bounds-check --pointer-check
