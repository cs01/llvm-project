#!/bin/bash
# The Lean gate: the model builds, every file in SafeC/ is imported (lake builds only what SafeC.lean reaches), and no proof is
# skipped or assumed.
set -o pipefail; cd "$(dirname "$0")" || exit 1; bad=0
for f in SafeC/*.lean; do m=SafeC.$(basename "$f" .lean); grep -rqw "import $m" SafeC.lean SafeC || { echo "not imported: $f"; bad=1; }; done
grep -rnwE 'sorry|admit|axiom|native_decide|implemented_by|extern' SafeC SafeC.lean && bad=1
lake build 2>&1 | tail -3 || bad=1
exit $bad
