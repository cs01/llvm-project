#!/bin/sh
# Everything -fc-contracts does today. Point CLANG at a build of this branch.
set -e

CLANG=${CLANG:-$(dirname "$0")/../build-arm/bin/clang}
if [ ! -x "$CLANG" ]; then
  echo "$0: no clang at '$CLANG'. Build this branch, then set CLANG to its bin/clang." >&2
  exit 1
fi
# Absolute before the cd, so a relative CLANG still points where the caller meant.
CLANG=$(cd "$(dirname "$CLANG")" && pwd)/$(basename "$CLANG")
cd "$(dirname "$0")"

if [ -t 1 ]; then B=$(printf '\033[1m'); D=$(printf '\033[2m'); R=$(printf '\033[0m')
else B=; D=; R=; fi
section() { printf '\n%s──  %s%s\n\n' "$B" "$1" "$R"; }
note()    { printf '%s%s%s\n' "$D" "$1" "$R"; }

# -ast-dump-filter prints every redeclaration; keep the prototype, which is the
# one carrying the clauses. Addresses are stripped so two dumps can be compared.
first_decl() { awk '/^Dumping /{n++} n>1{exit} {print}' | sed 's/ 0x[0-9a-f]*//g'; }
# PCH bookkeeping that says nothing about the contract: the 'imported' marker on
# every deserialized decl, and the header spelled absolute rather than as included.
strip_pch_noise() { sed -e 's/ imported//' -e "s|<$PWD/|<./|"; }

dump_decompress() {
  "$CLANG" -Xclang -ast-dump -Xclang -ast-dump-filter=decompress \
           -fsyntax-only -fc-contracts contracts.c | first_decl
}

section "Contracts are accepted, and land in the AST"
"$CLANG" -fsyntax-only -fc-contracts contracts.c
note "clang -fsyntax-only -fc-contracts contracts.c  ->  no diagnostics"
echo
note "Each clause is a real AST node on the FunctionDecl: 'old(dstCap)' is a"
note "ContractOldExpr, and 'r' an implicit VarDecl bound to the return value."
echo
dump_decompress

section "Contracts are checked at every call site, at compile time"
"$CLANG" -fsyntax-only -fc-contracts checked.c 2>&1 || true

section "The same clauses lower to CBMC, so they can be proved"
"$CLANG" -fsyntax-only -fc-contracts -fcontract-emit-cprover contracts.h

section "Every rule the front end enforces"
"$CLANG" -fsyntax-only -fc-contracts mistakes.c 2>&1 || true

section "Contracts survive a precompiled header"
"$CLANG" -cc1 -fc-contracts -emit-pch -o contracts.pch contracts.h
"$CLANG" -cc1 -fc-contracts -include-pch contracts.pch -ast-dump-all \
         -ast-dump-filter=decompress contracts.c | first_decl | strip_pch_noise > pch.ast
dump_decompress > direct.ast
if diff -u direct.ast pch.ast > pch.diff; then
  echo "The decompress contract read back out of the PCH matches the one parsed"
  echo "from source, node for node: same pre/post clauses, same ContractOldExpr."
else
  echo "MISMATCH: the contract AST changed across a PCH round trip." >&2
  cat pch.diff >&2
fi
rm -f contracts.pch pch.ast direct.ast pch.diff
echo
