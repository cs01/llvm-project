#!/bin/sh
# FINDING: inflate_table's doc-comment says callers need 2^bits entries. Size an
# array that way and the body writes past it.
# Full write-up: ../generalize/zlib/FINDING-inflate-table-doc.md
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ZLIB=${ZLIB:-$HOME/zlib}
[ -d "$ZLIB" ] || { echo "SKIP 02-zlib-inflate-table: no zlib tree at $ZLIB"; exit 0; }

W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
echo "== zlib inflate_table: 2^bits is not the contract =="

build() { # $1 = table size expression
  sed "s/static code table\[1u << ROOT_BITS\];/static code table[$1];/" \
      "$HERE/../generalize/zlib/harness_inflate_table.c" > "$W/t.c"
  cc -E -DNDEBUG -I "$ZLIB" "$W/t.c" -o "$W/t.i" 2>/dev/null
  { echo 'void __CPROVER_assume(int);'; cat "$W/t.i"; } > "$W/t2.i"
  goto-cc "$W/t2.i" -o "$W/t.goto" 2>/dev/null
}

for N in "1u << ROOT_BITS" 16 32; do
  build "$N"
  R=$(timeout 600 cbmc "$W/t.goto" --function harness --bounds-check \
        --pointer-check --pointer-overflow-check --unwind 20 \
        --no-unwinding-assertions 2>&1)
  printf '  table[%-14s] %s\n' "$N" "$(printf '%s' "$R" | grep -oE '\*\* [0-9]+ of [0-9]+ failed')"
  printf '%s' "$R" | grep -E "dereference failure.*next\[" | sed 's/^/      /'
done
echo "  (2^bits sizes the ROOT table only; a code longer than bits needs a sub-table past it)"
