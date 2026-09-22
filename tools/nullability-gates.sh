#!/bin/bash
# Gates for nullability analysis changes (see docs/nullability-safety-plan.md).
# Builds clang, runs the nullability lit tests, writes sorted sqlite warning
# and evidence lists tagged <tag>, and checks clang-format. Compare two tags
# with --diff to see every gained or lost warning.
#
# Usage:
#   tools/nullability-gates.sh <tag>              # build + gates, lists saved as <tag>
#   tools/nullability-gates.sh --diff <old> <new>  # gained/lost lines between two tags
#
# Env: BUILD_DIR (default build-arm), SQLITE (path to the sqlite3.c
# amalgamation, default ~/git/sqlite/sqlite3.c), OUT (default /tmp/nullability-gates).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
BUILD_DIR="${BUILD_DIR:-build-arm}"
SQLITE="${SQLITE:-$HOME/git/sqlite/sqlite3.c}"
OUT="${OUT:-/tmp/nullability-gates}"
mkdir -p "$OUT"
export LC_ALL=C

if [[ "${1:-}" == "--diff" ]]; then
  for m in nonnull nullable evidence; do
    old="$OUT/sqlite-$m-$2.txt" new="$OUT/sqlite-$m-$3.txt"
    echo "=== $m: lost $(comm -23 "$old" "$new" | wc -l) gained $(comm -13 "$old" "$new" | wc -l)"
    comm -3 "$old" "$new" | sed "s|$(dirname "$SQLITE")/||" | head -40
  done
  exit 0
fi

TAG="${1:?usage: nullability-gates.sh <tag> | --diff <old> <new>}"
if ! cmake --build "$BUILD_DIR" --target clang -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >"$OUT/build-$TAG.log" 2>&1; then
  grep -E "error:" "$OUT/build-$TAG.log" | head -20
  echo "BUILD FAILED (log: $OUT/build-$TAG.log)"
  exit 1
fi

TESTS=$(ls clang/test/*/flow-nullability* clang/test/Driver/nullsafe* \
  clang/test/SemaCXX/nullability-default* 2>/dev/null | grep -v '\.h$')
"$BUILD_DIR/bin/llvm-lit" -q $TESTS
echo "lit exit: $?"

CLANG="$BUILD_DIR/bin/clang"
SYSROOT_FLAGS=()
command -v xcrun >/dev/null && SYSROOT_FLAGS=(-isysroot "$(xcrun --show-sdk-path)")
if [[ -f "$SQLITE" ]]; then
  for mode in nonnull nullable; do
    "$CLANG" "${SYSROOT_FLAGS[@]}" -fsyntax-only -fflow-sensitive-nullability \
      -fnullability-default=$mode -Wno-everything -Wflow-nullability \
      -ferror-limit=0 "$SQLITE" 2>&1 | grep 'warning:' | sort >"$OUT/sqlite-$mode-$TAG.txt"
    echo "sqlite $mode: $(wc -l <"$OUT/sqlite-$mode-$TAG.txt")"
  done
  "$CLANG" "${SYSROOT_FLAGS[@]}" -fsyntax-only -fflow-sensitive-nullability \
    -fnullability-default=nonnull -Rnullsafe-evidence -Wno-everything \
    "$SQLITE" 2>&1 | grep 'remark:' | sort >"$OUT/sqlite-evidence-$TAG.txt"
  echo "sqlite evidence: $(wc -l <"$OUT/sqlite-evidence-$TAG.txt")"
else
  echo "sqlite gate SKIPPED: $SQLITE not found (cd sqlite && ./configure && make sqlite3.c)"
fi
echo "clang-format diff lines: $(git clang-format --diff HEAD 2>/dev/null | grep -c '^[+-]')"
