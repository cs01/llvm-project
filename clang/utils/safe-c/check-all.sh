#!/bin/bash
# Every Safe C gate, against one built compiler, with one line per gate. Exits non-zero if any gate fails.
#   clang/utils/safe-c/check-all.sh            all gates
#   clang/utils/safe-c/check-all.sh lit docs   only the gates named
# SAFEC=path/to/clang picks the compiler (default build.noindex/bin/clang); the lit gate uses that build's llvm-lit.
R=$(cd "$(dirname "$0")/../../.." && pwd); cd "$R" || exit 1
export SAFEC=${SAFEC:-$R/build.noindex/bin/clang}; export SDKROOT=${SDKROOT:-$(xcrun --show-sdk-path 2>/dev/null)}
B=$(dirname "$(dirname "$SAFEC")")
LOG=$(mktemp -d); fails=0

# name | command. Each command's last line of output is shown; its exit status decides pass or fail.
GATES=(
  "lit|$B/bin/llvm-lit -s clang/test/*/safe-c-* clang/test/Sema/LifetimeSafety clang/test/BoundsSafety"
  "headers|cmp clang/lib/Headers/safec.h safe-c-std/safec.h && echo 'safec.h: both copies identical'"
  "libc|bash clang/utils/safe-c/libc/check.sh"
  "misuse|bash safe-c-std/misuse/check.sh"
  "docs|bun clang/utils/safe-c/docs-check.ts"
  "corpus|bun clang/utils/safe-c/corpus/check.ts"
  "holes|bash clang/utils/safe-c/holes/check.sh"
  "cve|bash clang/utils/safe-c/cve/run.sh"
  "ports|bash safe-c-notes/ports/test-ports.sh"
  "lean|cd safe-c-lean && lake build 2>&1 | tail -3 && ! grep -rn sorry SafeC"
)

want=" $* "
printf '%-8s %-6s %6s  %s\n' gate result secs "last line"
for g in "${GATES[@]}"; do
  name=${g%%|*}; cmd=${g#*|}
  [ $# -gt 0 ] && [[ "$want" != *" $name "* ]] && continue
  t0=$(date +%s)
  bash -c "$cmd" >"$LOG/$name" 2>&1; rc=$?
  t=$(( $(date +%s) - t0 ))
  [ $rc = 0 ] && res=pass || { res=FAIL; fails=$((fails + 1)); }
  printf '%-8s %-6s %6s  %s\n' "$name" "$res" "$t" "$(grep -v '^\s*$' "$LOG/$name" | tail -1 | cut -c1-110)"
done
[ $fails -gt 0 ] && echo "$fails gate(s) failed; full output in $LOG"
exit $((fails > 0))
