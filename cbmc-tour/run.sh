#!/usr/bin/env bash
# Run every example in this tour and print what CBMC reports.
#
#   sudo apt-get install cbmc     # or build from https://github.com/diffblue/cbmc
#   ./run.sh
#
# Each section maps to a numbered section of README.md.
set -u

cd "$(dirname "$0")" || exit 1
EX=examples
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

command -v cbmc >/dev/null || { echo "cbmc not on PATH"; exit 1; }

hdr() { printf '\n\033[1m=== %s ===\033[0m\n' "$*"; }
run() { printf '\n$ %s\n' "$*"; "$@" 2>&1 | sed -n '/\*\* Results/,$p;/\*\* coverage/,$p'; }

echo "using $(cbmc --version)"

hdr "1. First proof: -INT_MIN overflows (README 2)"
run cbmc $EX/01_first.c --trace
run cbmc $EX/01_first.c --signed-overflow-check

hdr "2. Proof harness: nondet inputs + assume + assert (README 4)"
run cbmc $EX/02_harness.c --trace
echo "--- and the fixed version, proved over all 2^64 input pairs ---"
run cbmc $EX/02b_fixed.c

hdr "3. GOTO program: control flow becomes explicit gotos (README 3a)"
goto-cc -o "$WORK/03.goto" $EX/03_loop.c
printf '\n$ goto-instrument --show-goto-functions 03.goto\n'
goto-instrument --show-goto-functions "$WORK/03.goto" 2>/dev/null |
  sed -n '/^sum_to /,/END_FUNCTION/p'

hdr "4. The SSA equation handed to the solver (README 3b)"
printf '\n$ cbmc 05_tiny.c --show-vcc\n'
cbmc $EX/05_tiny.c --show-vcc 2>/dev/null | sed -n '/VERIFICATION CONDITIONS/,$p'

hdr "5. The bound, and --unwinding-assertions (README 6)"
echo "--- bound too small, no unwinding assertions: a false SUCCESS ---"
run cbmc $EX/03_loop.c --unwind 3
echo "--- same bound, with unwinding assertions: honest ---"
run cbmc $EX/03_loop.c --unwind 3 --unwinding-assertions
echo "--- sufficient bound: a real proof ---"
run cbmc $EX/03_loop.c --unwind 7 --unwinding-assertions

hdr "6. Memory safety checks (README 5)"
run cbmc $EX/04_memory.c --pointer-check --bounds-check --memory-leak-check --unwind 6

hdr "7. Property selection (README 7)"
printf '\n$ cbmc 04_memory.c --pointer-check --show-properties\n'
cbmc $EX/04_memory.c --pointer-check --show-properties 2>/dev/null |
  sed -n '/^Property/,$p' | head -12
run cbmc $EX/04_memory.c --pointer-check --unwind 6 \
    --property dup_prefix.pointer_dereference.5

hdr "8. Coverage / test generation (README 7)"
run cbmc $EX/03_loop.c --cover branch --unwind 7

hdr "9. Function contracts (README 8)"
goto-cc -o "$WORK/07.goto"  $EX/07_contract.c
goto-cc -o "$WORK/07b.goto" $EX/07b_contract_violated.c
goto-cc -o "$WORK/07c.goto" $EX/07c_unbounded_loop.c

echo "--- enforce: does the body satisfy its contract? ---"
goto-instrument --dfcc main --enforce-contract twice \
    "$WORK/07.goto" "$WORK/07e.goto" >/dev/null 2>&1
cbmc "$WORK/07e.goto" 2>&1 | grep -E 'postcondition|^\*\* [0-9]+ of|VERIFICATION'

echo "--- same, with a body that violates the ensures clause ---"
goto-instrument --dfcc main --enforce-contract twice \
    "$WORK/07b.goto" "$WORK/07be.goto" >/dev/null 2>&1
cbmc "$WORK/07be.goto" 2>&1 | grep -E 'postcondition|^\*\* [0-9]+ of|VERIFICATION'

echo "--- replace: callers use the contract, so an unbounded loop needs no --unwind ---"
goto-instrument --dfcc main --replace-call-with-contract twice \
    "$WORK/07c.goto" "$WORK/07cr.goto" >/dev/null 2>&1
cbmc "$WORK/07cr.goto" 2>&1 | grep -E 'main.assertion|^\*\* [0-9]+ of|VERIFICATION'

echo "--- plain BMC on the same code: the bound is the limit ---"
cbmc "$WORK/07c.goto" --unwind 5 --unwinding-assertions 2>&1 |
  grep -E 'main.assertion|unwind\.|^\*\* [0-9]+ of|VERIFICATION'

hdr "10. Driving CBMC programmatically via --json-ui (README 10a)"
python3 api/drive_json.py $EX/02_harness.c

echo
echo "Note: examples/06_contract.c is deliberately left out of this script --"
echo "is_fresh + a loop invariant over pointers takes minutes to instrument"
echo "and solve. See README section 8."
