#!/bin/sh
# Everything -fc-contracts does today. Point CLANG at a build of this branch.
set -e
CLANG=${CLANG:-$(dirname "$0")/../build-arm/bin/clang}
cd "$(dirname "$0")"

echo "== contracts are accepted and land in the AST =="
"$CLANG" -fsyntax-only -fc-contracts contracts.c && echo "   no diagnostics"
"$CLANG" -Xclang -ast-dump -fsyntax-only -fc-contracts contracts.c \
  | grep -E 'pre:|post:|ContractOldExpr|implicit used r'

echo
echo "== contracts are checked at every call site, at compile time =="
"$CLANG" -fsyntax-only -fc-contracts checked.c 2>&1 || true

echo
echo "== the same clauses lower to CBMC, so they can be proved =="
"$CLANG" -fsyntax-only -fc-contracts -fcontract-emit-cprover contracts.h

echo
echo "== every rule the front end enforces =="
"$CLANG" -fsyntax-only -fc-contracts mistakes.c 2>&1 || true

echo
echo "== contracts survive a precompiled header =="
"$CLANG" -cc1 -fc-contracts -emit-pch -o contracts.pch contracts.h
"$CLANG" -cc1 -fc-contracts -include-pch contracts.pch -ast-dump-all contracts.c \
  | grep -E 'post:|ContractOldExpr' | head -4
rm -f contracts.pch
