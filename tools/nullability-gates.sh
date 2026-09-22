#!/bin/bash
# Gates for nullability analysis changes (see docs/nullability-safety-plan.md).
# Builds clang, runs the nullability lit tests, writes sorted sqlite warning
# and evidence lists tagged <tag>, and checks clang-format. Exits nonzero if
# any gate fails. Compare two tags with --diff to see every gained or lost
# warning.
#
# Usage:
#   tools/nullability-gates.sh [--skip-sqlite] <tag>         # working tree
#   tools/nullability-gates.sh [--skip-sqlite] --base <tag>  # HEAD
#   tools/nullability-gates.sh --diff <old> <new>  # gained/lost lines between two tags
#
# --base sets uncommitted and untracked changes aside in a stash entry, runs
# the gates, then restores exactly that entry by its commit hash. A missing
# sqlite amalgamation fails the gates unless --skip-sqlite is given.
#
# Env: BUILD_DIR (default build-arm if present, else build), SQLITE (path to
# the sqlite3.c amalgamation, default ~/git/sqlite/sqlite3.c), OUT (default
# /tmp/nullability-gates).
set -uo pipefail
cd "${NULLABILITY_GATES_REPO:-$(dirname "${BASH_SOURCE[0]}")/..}" || exit 1
export NULLABILITY_GATES_REPO="$PWD"
[[ -d build-arm ]] && DEFAULT_BUILD_DIR=build-arm || DEFAULT_BUILD_DIR=build
BUILD_DIR="${BUILD_DIR:-$DEFAULT_BUILD_DIR}"
SQLITE="${SQLITE:-$HOME/git/sqlite/sqlite3.c}"
OUT="${OUT:-/tmp/nullability-gates}"
mkdir -p "$OUT"
export LC_ALL=C

SKIP_SQLITE=0
ARGS=()
for a in "$@"; do
  [[ "$a" == "--skip-sqlite" ]] && SKIP_SQLITE=1 || ARGS+=("$a")
done
set -- "${ARGS[@]}"

if [[ "${1:-}" == "--diff" ]]; then
  for m in nonnull nullable evidence; do
    old="$OUT/sqlite-$m-$2.txt" new="$OUT/sqlite-$m-$3.txt"
    echo "=== $m: lost $(comm -23 "$old" "$new" | wc -l) gained $(comm -13 "$old" "$new" | wc -l)"
    comm -3 "$old" "$new" | sed "s|$(dirname "$SQLITE")/||" | head -40
  done
  exit 0
fi

if [[ "${1:-}" == "--base" ]]; then
  TAG="${2:?usage: nullability-gates.sh --base <tag>}"
  STASH_MSG="nullability-gates --base $TAG $$"
  SELF="$OUT/nullability-gates-$$.sh"
  cp tools/nullability-gates.sh "$SELF"
  FORWARD=()
  [[ $SKIP_SQLITE -eq 1 ]] && FORWARD=(--skip-sqlite)
  if [[ -n "$(git status --porcelain)" ]]; then
    git stash push -q -u -m "$STASH_MSG" || exit 1
    STASH_SHA=$(git rev-parse --verify -q refs/stash)
    if [[ -z "$STASH_SHA" || "$(git log -1 --format=%s "$STASH_SHA")" != *"$STASH_MSG" ]]; then
      echo "could not identify the stash entry just created; nothing restored"
      exit 1
    fi
    restore() {
      if ! git stash apply -q --index "$STASH_SHA"; then
        echo "RESTORE FAILED: changes are in stash commit $STASH_SHA"
        return 1
      fi
      local ref
      ref=$(git stash list --format='%gd %H' | awk -v s="$STASH_SHA" '$2 == s { print $1; exit }')
      [[ -n "$ref" ]] && git stash drop -q "$ref"
    }
    trap restore EXIT
  fi
  bash "$SELF" "${FORWARD[@]}" "$TAG"
  exit $?
fi

TAG="${1:?usage: nullability-gates.sh [--skip-sqlite] <tag> | --base <tag> | --diff <old> <new>}"
FAILED=()

if ! cmake --build "$BUILD_DIR" --target clang -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >"$OUT/build-$TAG.log" 2>&1; then
  grep -E "error:" "$OUT/build-$TAG.log" | head -20
  echo "BUILD FAILED (log: $OUT/build-$TAG.log)"
  exit 1
fi

TESTS=$(ls clang/test/*/flow-nullability* clang/test/Driver/nullsafe* \
  clang/test/SemaCXX/nullability-default* 2>/dev/null | grep -v '\.h$')
"$BUILD_DIR/bin/llvm-lit" -q $TESTS || FAILED+=(lit)

CLANG="$BUILD_DIR/bin/clang"
SYSROOT_FLAGS=()
command -v xcrun >/dev/null && SYSROOT_FLAGS=(-isysroot "$(xcrun --show-sdk-path)")
run_sqlite() {
  local out="$1"
  shift
  "$CLANG" "${SYSROOT_FLAGS[@]}" -fsyntax-only -fflow-sensitive-nullability \
    -Wno-everything -ferror-limit=0 "$@" "$SQLITE" >"$out.raw" 2>&1
  local status=$?
  if [[ $status -ne 0 ]] || grep -q 'error:' "$out.raw"; then
    grep -m5 -E 'error:|Assertion|Stack dump' "$out.raw"
    return 1
  fi
}
if [[ $SKIP_SQLITE -eq 1 ]]; then
  echo "sqlite SKIPPED (--skip-sqlite)"
elif [[ -f "$SQLITE" ]]; then
  for mode in nonnull nullable; do
    f="$OUT/sqlite-$mode-$TAG.txt"
    run_sqlite "$f" -fnullability-default=$mode -Wflow-nullability || FAILED+=("sqlite-$mode")
    { grep 'warning:' "$f.raw" || true; } | sort >"$f"
    echo "sqlite $mode: $(wc -l <"$f")"
  done
  f="$OUT/sqlite-evidence-$TAG.txt"
  run_sqlite "$f" -fnullability-default=nonnull -Rnullsafe-evidence || FAILED+=(sqlite-evidence)
  { grep 'remark:' "$f.raw" || true; } | sort >"$f"
  echo "sqlite evidence: $(wc -l <"$f")"
else
  echo "sqlite not found: $SQLITE (cd sqlite && ./configure && make sqlite3.c)"
  FAILED+=(sqlite-missing)
fi

FORMAT_OUT=$(git clang-format --diff HEAD 2>&1)
FORMAT_STATUS=$?
FORMAT_LINES=$({ grep '^[+-]' <<<"$FORMAT_OUT" || true; } | wc -l)
echo "clang-format diff lines: $FORMAT_LINES"
if [[ "$FORMAT_LINES" -ne 0 ]]; then
  FAILED+=(clang-format)
elif [[ $FORMAT_STATUS -ne 0 && "$FORMAT_OUT" != *"no modified files to format"* && "$FORMAT_OUT" != *"did not modify any files"* ]]; then
  echo "$FORMAT_OUT" | head -5
  FAILED+=(clang-format-error)
fi

if [[ ${#FAILED[@]} -gt 0 ]]; then
  echo "GATES FAILED: ${FAILED[*]}"
  exit 1
fi
echo "GATES PASSED"
