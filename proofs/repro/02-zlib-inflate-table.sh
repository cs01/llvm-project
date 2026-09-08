#!/bin/sh
# FINDING: inflate_table's doc-comment says callers need 2^bits entries. State
# that as a contract, and the body writes past it.
#
# The contract is the whole reproduction. There is no harness here and no
# __CPROVER_assume: the clauses below are read off the doc-comment, clang lowers
# them and generates the entry point, CBMC discharges it.
#
# Full write-up: ../generalize/zlib/FINDING-inflate-table-doc.md
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ZLIB=${ZLIB:-$HOME/zlib}
[ -d "$ZLIB" ] || { echo "SKIP 02-zlib-inflate-table: no zlib tree at $ZLIB"; exit 0; }

echo "== zlib inflate_table: 2^bits is not the contract =="
echo "   contract under test, on inflate_table in $ZLIB/inftrees.c:"
sed -n '/^int ZLIB_INTERNAL inflate_table/,/^{$/p' "$ZLIB/inftrees.c" |
  grep -E "^\s+(pre|post|assigns) " | sed 's/^/     /'
grep -q 'pre (fresh(\*table' "$ZLIB/inftrees.c" 2>/dev/null || {
  echo "   inftrees.c is not annotated; apply ../generalize/zlib/annotate-inflate-table.patch"
  exit 0; }

# The control matters more than the red. A finding that does not go away when
# the clause under test is widened is not a finding about that clause.
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
cp "$ZLIB/inftrees.c" "$W/orig.c"
for N in "1u << 3" "16u"; do
  cp "$W/orig.c" "$W/inftrees.c"
  perl -0pi -e "s/pre \(fresh\(\*table, \(.*?\) \* sizeof\(code\)\)\)/pre (fresh(*table, ($N) * sizeof(code)))/s" \
      "$W/inftrees.c"
  printf '   table entries = %-8s ' "$N"
  UNWIND=20 DEADLINE=${DEADLINE:-1800} "$HERE/../verify-contract.sh" \
      inflate_table "$W/inftrees.c" -I "$ZLIB" 2>&1 |
    grep -E "^\*\* [0-9]+ of|solved by" | tr '\n' ' '
  echo
done

cat <<'NOTE'
   2^bits sizes the ROOT table only; a code longer than bits needs a sub-table
   past it. Widening the clause turns the proof green, which is what makes the
   red a statement about that clause and not about the rest of the contract.
NOTE
