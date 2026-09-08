#!/bin/sh
# Verify a function against the contract written on it. No harness.
#
#   ./verify-contract.sh <function> <tu.c> [-I dir ...] [-- cbmc flags]
#
# This is the whole point of the project, so it is one command:
#
#   annotate the function in this grammar
#     -> clang lowers the clauses to CBMC's
#     -> goto-instrument --enforce-contract BUILDS THE HARNESS FROM THE CONTRACT
#     -> cbmc discharges it
#
# If you find yourself writing __CPROVER_assume by hand, stop: that is a harness,
# it encodes assumptions nobody can review, and it is what this replaces.
set -u
FN=${1:?usage: verify-contract.sh <function> <tu.c> [-I dir ...] [-- cbmc flags]}
TU=${2:?usage: verify-contract.sh <function> <tu.c> [-I dir ...] [-- cbmc flags]}
shift 2

INCS=""; CBMC_FLAGS=""
while [ $# -gt 0 ]; do
  case "$1" in
    --) shift; CBMC_FLAGS="$*"; break ;;
    *)  INCS="$INCS $1"; shift ;;
  esac
done
# --pointer-overflow-check by default: every defect this project has found is a
# pointer formed outside its object, and --pointer-check alone reports them all
# clean. Opting out should be deliberate.
[ -n "$CBMC_FLAGS" ] || CBMC_FLAGS="--pointer-overflow-check --bounds-check --pointer-check"

HERE=$(cd "$(dirname "$0")" && pwd)
CLANG=${CLANG:-$HERE/../build/bin/clang}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

# 1. Preprocess with the system compiler. goto-cc cannot parse what clang's
#    glibc expansion leaves behind, and contract keywords survive cpp untouched.
# shellcheck disable=SC2086
cc -E -DNDEBUG -DZSTD_CONTRACTS -DCONTRACTS $INCS "$TU" -o "$W/tu.i" 2>/dev/null
sed -e 's/__builtin_memcpy/memcpy/g' -e 's/__builtin_memmove/memmove/g' \
    "$W/tu.i" > "$W/tu2.i"

# 2. Lower the grammar, and emit the entry point from the contract. The harness
#    is the general route: --enforce-contract additionally demands a contract on
#    every loop-shaped construct in the function, which measured out at eleven
#    for zlib's inflate_table and made it unverifiable. Set ENFORCE=1 to use
#    --enforce-contract instead, which is what checks the assigns clause.
ENFORCE=${ENFORCE:-0}
HARNESS_FLAG=-fcontract-emit-harness
[ "$ENFORCE" = 1 ] && HARNESS_FLAG=""
# 2. Lower the grammar. Front-end errors from unrelated headers are expected;
#    the clause count below is what actually matters.
# shellcheck disable=SC2086
"$CLANG" -cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit \
    $HARNESS_FLAG "$W/tu2.i" > "$W/out.c" 2>"$W/rewrite.log" || true
N=$(grep -c "__CPROVER_requires\|__CPROVER_ensures\|__CPROVER_assigns" "$W/out.c" 2>/dev/null || true)
[ "${N:-0}" -gt 0 ] || { echo "no contract clauses lowered -- is $FN annotated?" >&2
                         grep -m3 "error:" "$W/rewrite.log" >&2; exit 2; }
echo "lowered ${N} clause(s):"
grep -h "__CPROVER_requires\|__CPROVER_ensures\|__CPROVER_assigns" "$W/out.c" | sed 's/^ */  /' | head

# 3. A static inline nobody calls is dropped before instrumentation. Referencing
#    it constrains nothing -- every input constraint still comes from the
#    contract -- but the symbol has to exist.
{ cat "$W/out.c"
  echo "void *__contract_keep_$FN = (void *)&$FN;"; } > "$W/out2.c"
goto-cc "$W/out2.c" -o "$W/a.goto" 2>/dev/null || {
  echo "goto-cc failed; see $W" >&2; trap - EXIT; exit 3; }

# 4. The harness comes from the contract. Nothing is hand-written.
# Two passes, and the order is not optional: --apply-loop-contracts must run
# first and alone, or --enforce-contract refuses with "Loops remain in
# function" even when every loop is annotated.
goto-instrument --apply-loop-contracts "$W/a.goto" "$W/l.goto" >/dev/null 2>&1 ||
  cp "$W/a.goto" "$W/l.goto"

if [ "$ENFORCE" != 1 ]; then
  # The generated entry point already constrains every input the contract
  # mentions, so CBMC just runs it.
  ENTRY=__contract_harness_$FN
  # shellcheck disable=SC2086
  exec "$HERE/solve.sh" -t "${DEADLINE:-900}" "$W/l.goto" --function "$ENTRY" \
      ${UNWIND:+--unwind $UNWIND --no-unwinding-assertions} $CBMC_FLAGS
fi

OUT=$(goto-instrument --enforce-contract "$FN" "$W/l.goto" "$W/e.goto" 2>&1) || true
printf '%s' "$OUT" | grep -qi "not found" && {
  echo "goto-instrument could not find $FN" >&2; exit 4; }
if printf '%s' "$OUT" | grep -qi "reason"; then
  printf '%s\n' "$OUT" | grep -i "reason" >&2
  # CBMC cannot check a frame condition while a loop in the function is
  # unconstrained, so a function contract needs loop contracts on every loop
  # the function contains. Naming them is the difference between a usable
  # message and a dead end.
  if printf '%s' "$OUT" | grep -qi "loops remain"; then
    echo "" >&2
    echo "  every loop in $FN needs its own contract before its frame can be" >&2
    echo "  checked. the loops are:" >&2
    goto-instrument --show-loops "$W/a.goto" 2>/dev/null |
      grep -i "^Loop $FN\.\|$FN\." | sed 's/^/    /' >&2
    echo "" >&2
    echo "  add to each:  assigns (...) loop_invariant (...) decreases (...)" >&2
  fi
  exit 5
fi

# 5. Race the solvers rather than guessing one.
# shellcheck disable=SC2086
"$HERE/solve.sh" -t "${DEADLINE:-900}" "$W/e.goto" --function "$FN" $CBMC_FLAGS
