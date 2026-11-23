#!/bin/bash
# Rebuilds the nullsafe-upstream branch from nullability-safety,
# including only the core compiler changes suitable for an upstream PR.
# All fork-specific files (playground, install scripts, CI, WASM hacks,
# docs, benchmarks, etc.) are excluded.
#
# Usage:
#   ./tools/sync-upstream.sh            # rebuild to -wip branch, push, return to dev
#   ./tools/sync-upstream.sh --pr       # rebuild to nullsafe-upstream (updates the llvm PR!)
#   ./tools/sync-upstream.sh --dry-run  # show what would be included without creating the branch
#   ./tools/sync-upstream.sh --no-push  # rebuild but don't push

set -euo pipefail

DEV_BRANCH="nullability-safety"
# default to -wip so we don't accidentally update the llvm PR
UPSTREAM_BRANCH="nullsafe-upstream-wip"
# The dev branch is kept linear on top of upstream (rebase-upstream.yml), so
# the base is the newest first-parent commit that is upstream: written by
# someone else, or squash-merged from an llvm PR (subject ends in "(#N)").
# This needs no fetch: a stale local llvm/main would otherwise yield an old
# merge-base and drag thousands of unrelated upstream files into the PR.
FORK_AUTHORS="${FORK_AUTHORS:-Chad Smith,cs01}"
MAX_INCLUDE_FILES="${MAX_INCLUDE_FILES:-300}"
BASE_REF="$(git rev-list --first-parent --max-count=5000 --format='%H%x09%an%x09%s' "$DEV_BRANCH" |
    awk -F'\t' -v authors="$FORK_AUTHORS" '
        BEGIN { n = split(authors, a, ","); for (i = 1; i <= n; i++) fork[a[i]] = 1 }
        /^commit / { next }
        !found && (!($2 in fork) || $3 ~ /\(#[0-9]+\)$/) { print $1; found = 1 }')"
if [[ -z "$BASE_REF" ]]; then
    echo "ERROR: no upstream commit within 5000 first-parent commits of $DEV_BRANCH (FORK_AUTHORS=$FORK_AUTHORS)"
    exit 1
fi
OTHER_AUTHORS=$(git log --format='%an%x09%s' "$BASE_REF..$DEV_BRANCH" |
    awk -F'\t' -v authors="$FORK_AUTHORS" '
        BEGIN { n = split(authors, a, ","); for (i = 1; i <= n; i++) fork[a[i]] = 1 }
        !($1 in fork) || $2 ~ /\(#[0-9]+\)$/' | sort -u)
if [[ -n "$OTHER_AUTHORS" ]]; then
    echo "ERROR: $BASE_REF..$DEV_BRANCH contains upstream-looking commits:"
    echo "$OTHER_AUTHORS" | head -20
    echo "Set FORK_AUTHORS (comma-separated) or rebase $DEV_BRANCH onto upstream."
    exit 1
fi
echo "=== Base: $(git log -1 --format='%h %ci %s' "$BASE_REF") ($(git rev-list --count "$BASE_REF..$DEV_BRANCH") fork commits)"
if git rev-parse -q --verify llvm/main >/dev/null &&
    ! git merge-base --is-ancestor "$BASE_REF" llvm/main; then
    echo "note: local llvm/main does not contain the base; it is stale (ignored)"
fi

DRY_RUN=false
NO_PUSH=false
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=true ;;
        --no-push) NO_PUSH=true ;;
        --pr) UPSTREAM_BRANCH="nullsafe-upstream" ;;
    esac
done

# The upstream branch is an allowlist: only paths under these prefixes are
# carried over. A new fork-only file (playground, CI, scripts, docs) is left
# out by default instead of leaking into the llvm PR.
INCLUDE_PREFIXES=(
    'clang/include/clang/Analysis/'
    'clang/lib/Analysis/'
    'clang/include/clang/Basic/'
    'clang/include/clang/Options/'
    'clang/include/clang/Sema/'
    'clang/lib/Sema/'
    'clang/lib/Driver/'
    'clang/include/clang/ScalableStaticAnalysis/'
    'clang/lib/ScalableStaticAnalysis/'
    'clang/docs/'
    'clang/test/'
)
# Exceptions inside the allowlisted prefixes.
EXCLUDE_PATHS=(
    # needs a system C++ standard library, which upstream CI does not provide
    'clang/test/SemaCXX/nullability-safety-real-smartptr.cpp'
)

# get list of changed files relative to upstream
ALL_FILES=$(git diff --name-only "$BASE_REF" "$DEV_BRANCH")

INCLUDE_FILES=()
LEFT_OUT=()
for file in $ALL_FILES; do
    keep=false
    for prefix in "${INCLUDE_PREFIXES[@]}"; do
        [[ "$file" == "$prefix"* ]] && keep=true && break
    done
    for path in "${EXCLUDE_PATHS[@]}"; do
        [[ "$file" == "$path" ]] && keep=false && break
    done
    if [[ "$keep" == "true" ]]; then
        INCLUDE_FILES+=("$file")
    else
        LEFT_OUT+=("$file")
    fi
done

echo "=== Fork-only, left out (${#LEFT_OUT[@]}) ==="
printf '%s\n' "${LEFT_OUT[@]}"
echo ""
echo "=== Files to include in upstream PR (${#INCLUDE_FILES[@]}) ==="
printf '%s\n' "${INCLUDE_FILES[@]}"
echo ""

if [[ ${#INCLUDE_FILES[@]} -eq 0 ]]; then
    echo "ERROR: no files to include; check FORK_AUTHORS ($FORK_AUTHORS)"
    exit 1
fi
if [[ ${#INCLUDE_FILES[@]} -gt $MAX_INCLUDE_FILES ]]; then
    echo "ERROR: ${#INCLUDE_FILES[@]} files would be included (limit $MAX_INCLUDE_FILES); the base is probably wrong."
    echo "Rerun with MAX_INCLUDE_FILES=<n> if this is intended."
    exit 1
fi

if [[ "$DRY_RUN" == "true" ]]; then
    echo "(dry run — no branch created)"
    exit 0
fi

# confirm we're on the dev branch
CURRENT=$(git branch --show-current)
if [[ "$CURRENT" != "$DEV_BRANCH" ]]; then
    echo "ERROR: must be on $DEV_BRANCH (currently on $CURRENT)"
    exit 1
fi

# ensure no uncommitted changes
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "ERROR: uncommitted changes — commit or stash first"
    exit 1
fi

echo "=== Creating $UPSTREAM_BRANCH from $BASE_REF ==="

# delete old upstream branch if it exists
git branch -D "$UPSTREAM_BRANCH" 2>/dev/null || true

# create new branch from upstream base
git checkout "$BASE_REF" --detach
git checkout -b "$UPSTREAM_BRANCH"

# checkout only the upstream-worthy files from the dev branch
git checkout "$DEV_BRANCH" -- "${INCLUDE_FILES[@]}"

# commit
git commit -m "$(cat <<'EOF'
add flow-sensitive nullability analysis for C/C++

Adds a new compile-time analysis that detects null pointer dereferences
using flow-sensitive dataflow analysis on the CFG. The analysis tracks
nullability state through control flow, supporting null checks, early
returns, assertions, ternary operators, loops, and boolean intermediaries.

New flags:
  -fnullability-safety    enables the analysis
  -fnullability-default=<mode>    sets default nullability (nullable|nonnull|unspecified)

The analysis follows the same architecture as ThreadSafety and
UninitializedValues: a standalone analysis in lib/Analysis/ invoked
from AnalysisBasedWarnings.cpp, reporting via a handler interface.
EOF
)"

# push and return to dev branch
if [[ "$NO_PUSH" == "false" ]]; then
    echo "=== Pushing $UPSTREAM_BRANCH ==="
    git push --force-with-lease origin "$UPSTREAM_BRANCH"
fi

echo "=== Returning to $DEV_BRANCH ==="
git checkout "$DEV_BRANCH"

echo ""
echo "=== Done ==="
echo "Branch '$UPSTREAM_BRANCH' synced with ${#INCLUDE_FILES[@]} files."
echo "To inspect:  git diff $BASE_REF...$UPSTREAM_BRANCH --stat"
