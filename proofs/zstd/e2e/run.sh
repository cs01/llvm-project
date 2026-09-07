#!/bin/sh
# End-to-end checks: what a C maintainer would require before annotating their
# own code. Several fail on purpose -- see README.md. Each case records the
# status it is expected to have, and the runner fails only on a mismatch, so a
# known-failing case turning green is a signal to come and update the record.
#
#   ZSTD=~/git/zstd CLANG=../../../build/bin/clang ./run.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CLANG=${CLANG:-clang}
ZSTD=${ZSTD:-$HOME/facebook/zstd}
WORK=$(mktemp -d)
FAILED=0

# Prefer an SMT solver on the symbolically-allocated harnesses; see COST.md.
SOLVER=""
command -v bitwuzla >/dev/null 2>&1 && SOLVER=--bitwuzla
[ -z "$SOLVER" ] && command -v z3 >/dev/null 2>&1 && SOLVER=--z3

report() { # name expected actual detail
  if [ "$2" = "$3" ]; then
    printf '  %-46s %-6s %s\n' "$1" "$3" "$4"
  else
    printf '  %-46s %-6s %s  <-- RECORDED %s\n' "$1" "$3" "$4" "$2"
    FAILED=$((FAILED + 1))
  fi
}

echo "== what a maintainer would require =="

# ---------------------------------------------------------------- case 1
# Contracts are declaration-level, so a build without -fc-contracts must be
# byte-identical: nobody adopts a spec language that changes their binary.
cat > "$WORK/c1.c" <<'EOF'
#if defined(__has_feature)
#  if __has_feature(c_contracts)
#    define PRE(x) pre (x)
#  endif
#endif
#ifndef PRE
#  define PRE(x)
#endif
int add(int a, int b) PRE(a > 0);
int add(int a, int b) { return a + b; }
EOF
$CLANG -fc-contracts -c "$WORK/c1.c" -o "$WORK/c1_on.o" 2>/dev/null
$CLANG                -c "$WORK/c1.c" -o "$WORK/c1_off.o" 2>/dev/null
if cmp -s "$WORK/c1_on.o" "$WORK/c1_off.o"; then A=PASS; else A=FAIL; fi
report "1 contracts cost nothing in a normal build" PASS "$A" "object code identical"

# ---------------------------------------------------------------- case 2
# The annotated zstd source should contain no prover vocabulary. The point of
# the extension is that a maintainer writes C, not CBMC.
N=$(grep -c "__CPROVER" "$HERE/../patches/annotate-wildcopy-our-grammar.patch" 2>/dev/null || echo 0)
if [ "$N" -eq 0 ]; then A=PASS; else A=FAIL; fi
report "2 no prover vocabulary in annotated source" FAIL "$A" "$N __CPROVER tokens in the patch"

# ---------------------------------------------------------------- case 3
# A do/while loop should be annotatable where it stands. Today the function has
# to be restructured for the contract to attach, which is a change a maintainer
# is being asked to make to shipping code for the benefit of a tool.
cat > "$WORK/c3.c" <<'EOF'
void f(unsigned char *b, unsigned n) {
  unsigned i = 0;
  do
    __CPROVER_assigns(i, __CPROVER_object_upto(b, n))
    __CPROVER_loop_invariant(i < n)
    __CPROVER_decreases(n - i)
  { b[i] = 0; i++; } while (i < n);
}
EOF
if goto-cc -c "$WORK/c3.c" -o "$WORK/c3.goto" 2>/dev/null &&
   goto-instrument --apply-loop-contracts "$WORK/c3.goto" "$WORK/c3i.goto" 2>&1 |
     grep -q "unsupported on do/while"; then A=FAIL; else A=PASS; fi
report "3 a loop verifies without restructuring" FAIL "$A" "goto-instrument rejects do/while"

# ---------------------------------------------------------------- case 4
# The scalability question, in two halves. Verification that must inline every
# callee costs the size of the program; verification that can replace a call
# with its contract costs one function at a time.
#
# Order matters and is not obvious: --apply-loop-contracts has to run in its own
# earlier pass, or --enforce-contract refuses with "Loops remain in function".
$CLANG -cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit \
    "$HERE/case4_modular.c" > "$WORK/c4.c" 2>/dev/null
A4a=FAIL; D4a="lowering or instrumentation failed"
A4b=FAIL; D4b="lowering or instrumentation failed"
if [ -s "$WORK/c4.c" ] && goto-cc "$WORK/c4.c" -o "$WORK/c4.goto" 2>/dev/null &&
   goto-instrument --apply-loop-contracts "$WORK/c4.goto" "$WORK/c4a.goto" >/dev/null 2>&1 &&
   goto-instrument --enforce-contract fill_zero "$WORK/c4a.goto" "$WORK/c4e.goto" >/dev/null 2>&1 &&
   goto-instrument --replace-call-with-contract fill_zero "$WORK/c4.goto" \
       "$WORK/c4r.goto" >/dev/null 2>&1; then
  # The callee has to hold up against its own contract first, or the rest is
  # meaningless.
  if timeout 900 cbmc "$WORK/c4e.goto" --function fill_zero --bounds-check \
       --pointer-check $SOLVER 2>&1 | grep -q "VERIFICATION SUCCESSFUL"; then
    OUT=$(timeout 900 cbmc "$WORK/c4r.goto" --function caller --bounds-check \
            --pointer-check $SOLVER 2>&1)
    printf '%s' "$OUT" | grep -q "stayed inside its frame: SUCCESS" &&
      { A4a=PASS; D4a="callee provably could not touch the caller's local"; } ||
      D4a="frame not honoured across the replacement"
    printf '%s' "$OUT" | grep -q "postcondition is usable by the caller: SUCCESS" &&
      { A4b=PASS; D4b="postcondition carried to the call site"; } ||
      D4b="$(printf '%s' "$OUT" | grep -oE 'pointer outside dynamic object bounds.*' | head -1)"
    [ -z "$D4b" ] && D4b="postcondition did not carry"
  else
    D4a="callee does not verify against its own contract"; D4b="$D4a"
  fi
fi
report "4a callee's frame holds at the call site" PASS "$A4a" "$D4a"
report "4b callee's postcondition holds at the site" FAIL "$A4b" "$D4b"

# ---------------------------------------------------------------- case 5
# A contract on a FORCE_INLINE function has to reach the sites it is inlined
# into. zstd's decoder has fifteen such uses; if each needs its invariant
# repeated by hand the annotation burden multiplies rather than being paid once.
cat > "$WORK/c5.c" <<'EOF'
static inline __attribute__((always_inline)) void inner(unsigned char *b, unsigned n) {
  unsigned i = 0;
  while (i < n)
    __CPROVER_assigns(i, __CPROVER_object_upto(b, n))
    __CPROVER_loop_invariant(i <= n)
    __CPROVER_decreases(n - i)
  { b[i] = 0; i++; }
}
void outer(unsigned char *b, unsigned n) { inner(b, n); }
EOF
A=FAIL; D="loop still unwound in the caller"
if goto-cc -c "$WORK/c5.c" -o "$WORK/c5.goto" 2>/dev/null &&
   goto-instrument --apply-loop-contracts "$WORK/c5.goto" "$WORK/c5i.goto" >/dev/null 2>&1; then
  OUT=$(timeout 300 cbmc "$WORK/c5i.goto" --function outer --bounds-check 2>&1)
  printf '%s' "$OUT" | grep -q "VERIFICATION SUCCESSFUL" && { A=PASS; D="proved with no unwind bound"; }
fi
report "5 contract survives into a FORCE_INLINE site" FAIL "$A" "$D"

# ---------------------------------------------------------------- case 6
# A wrong contract must fail. A tool that only ever agrees is worth nothing.
cat > "$WORK/c6.c" <<'EOF'
#include <stdlib.h>
int wrong(int n) __CPROVER_requires(n > 0) __CPROVER_ensures(__CPROVER_return_value > n)
{ return n; }
EOF
A=FAIL; D="a false postcondition verified"
if goto-cc -c "$WORK/c6.c" -o "$WORK/c6.goto" 2>/dev/null &&
   goto-instrument --enforce-contract wrong "$WORK/c6.goto" "$WORK/c6i.goto" >/dev/null 2>&1; then
  timeout 300 cbmc "$WORK/c6i.goto" --function wrong 2>&1 |
    grep -q "VERIFICATION FAILED" && { A=PASS; D="false postcondition rejected"; }
fi
report "6 a wrong contract fails loudly" PASS "$A" "$D"

# ---------------------------------------------------------------- case 7
# A proof nobody can afford to run is not part of anyone's CI.
A=FAIL; D="no wildcopy proof available"
if [ -x "$HERE/../run-wildcopy-from-grammar.sh" ] && [ -d "$ZSTD" ]; then
  T0=$(date +%s)
  if ZSTD="$ZSTD" CLANG="$CLANG" "$HERE/../run-wildcopy-from-grammar.sh" 2>/dev/null |
       grep -q "VERIFICATION SUCCESSFUL"; then
    E=$(( $(date +%s) - T0 ))
    D="${E}s"
    [ "$E" -le 60 ] && A=PASS || D="${E}s, over the 60s budget"
  fi
fi
report "7 a proof fits in a CI step (<= 60s)" PASS "$A" "$D"

# ---------------------------------------------------------------- case 8
# The annotations have to be able to live in the upstream source tree. A
# maintainer will not carry a syntax that breaks every build that is not this
# fork -- and today's contextual keywords are exactly that, which is why the
# zstd patch is wrapped in #ifdef. Measured against the stock toolchains a
# contributor actually has.
cat > "$WORK/c8.c" <<'EOF'
int f(int n) pre (n > 0);
EOF
A=FAIL; D="stock compilers reject the annotation"
if cc -fsyntax-only "$WORK/c8.c" >/dev/null 2>&1; then A=PASS; D="stock cc accepts it"; fi
report "8 annotations can live in upstream source" FAIL "$A" "$D"

echo
if [ "$FAILED" -eq 0 ]; then
  echo "all cases behaved as recorded"
else
  echo "$FAILED case(s) differ from the recorded status -- update e2e/README.md"
fi
rm -rf "$WORK"
exit "$FAILED"
