#!/bin/sh
# Demonstrates everything -fc-contracts currently does. Point CLANG at a build
# of this branch.
set -e
CLANG=${CLANG:-$(dirname "$0")/../build-arm/bin/clang}
cd "$(dirname "$0")"

echo "== 1. accepted, contracts on =="
"$CLANG" -fsyntax-only -fc-contracts codec.c && echo "   no diagnostics"

echo "== 2. accepted, contracts off (the shim expands to nothing) =="
"$CLANG" -fsyntax-only codec.c && echo "   no diagnostics"

echo "== 3. accepted by a compiler that has never heard of contracts =="
/usr/bin/cc -fsyntax-only codec.c && echo "   no diagnostics"

echo "== 4. the clauses are in the AST =="
"$CLANG" -Xclang -ast-dump -fsyntax-only -fc-contracts codec.c \
  | grep -E 'pre:|post:|ContractOldExpr|implicit used r'

echo "== 5. every rule that is enforced =="
"$CLANG" -fsyntax-only -fc-contracts mistakes.c 2>&1 || true

echo "== 6. contracts survive a PCH =="
"$CLANG" -cc1 -fc-contracts -emit-pch -o codec.pch codec.h
"$CLANG" -cc1 -fc-contracts -include-pch codec.pch -ast-dump-all codec.c \
  | grep -E 'post:|ContractOldExpr' | head -4
rm -f codec.pch
