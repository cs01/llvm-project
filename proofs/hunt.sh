#!/bin/sh
# Run every mechanizable detector in PATTERNS.md over a source tree.
#
#   ./hunt.sh ~/redis [more trees...]
#
# Output is triage, not findings. Every hit needs reading before it is believed:
# the detectors are text-level, and the recorded hit rate is 2 in 7. Hits already
# triaged as false positives are listed in FINDINGS.md -- check there before
# spending time on one.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
[ $# -ge 1 ] || { echo "usage: hunt.sh <source-tree> [...]" >&2; exit 2; }

for TREE in "$@"; do
  [ -d "$TREE" ] || { echo "skip: no tree at $TREE" >&2; continue; }
  echo "=== $TREE"
  for D in "$HERE"/detectors/*.py; do
    [ -f "$D" ] || continue
    echo "--- $(basename "$D" .py)"
    python3 "$D" "$TREE" 2>/dev/null | sed 's/^/  /'
  done
done

cat <<'EOT'

Next: confirm each hit by reading it, then either
  - file it under proofs/generalize/<project>/ and add a row to FINDINGS.md, or
  - add it to the false-positive table in FINDINGS.md so nobody re-triages it.
A cleared hit is worth recording. It is the cheaper half of the work.
EOT
