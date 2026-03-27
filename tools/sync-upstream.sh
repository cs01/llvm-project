#!/bin/bash
# Rebuilds the nullsafe-upstream branch from nullsafe-clang-dev,
# including only the core compiler changes suitable for an upstream PR.
# All fork-specific files (playground, install scripts, CI, WASM hacks,
# docs, benchmarks, etc.) are excluded.
#
# Usage:
#   ./tools/sync-upstream.sh            # rebuild, push, and return to dev branch
#   ./tools/sync-upstream.sh --dry-run  # show what would be included without creating the branch
#   ./tools/sync-upstream.sh --no-push  # rebuild but don't push

set -euo pipefail

DEV_BRANCH="nullsafe-clang-dev"
UPSTREAM_BRANCH="nullsafe-upstream"
BASE_REF="llvm/main"

DRY_RUN=false
NO_PUSH=false
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=true ;;
        --no-push) NO_PUSH=true ;;
    esac
done

# files/patterns to EXCLUDE from the upstream branch
EXCLUDE_PATTERNS=(
    # fork infrastructure
    '.github/'
    '.gitignore'
    'README.md'
    'CONTRIBUTING.md'
    'GRADUAL_MIGRATION.md'
    'PERFORMANCE.md'
    'COMMIT_MSG.txt'
    'install.sh'
    'install-interactive.sh'
    'tools/'
    'docs/'

    # playground
    'nullsafe-playground/'

    # claude/AI config
    '*.claude*'
    '*CLAUDE.md'

    # nullsafe-headers (fork-specific shim headers)
    'clang/nullsafe-headers/'

    # misc fork files
    'clang/cJSON.plist'
    'clang/.gitignore'
    'clang/test/Sema/.clangd'

    # benchmark scripts in test dir
    'clang/test/Sema/*benchmark*'

    # WASM build hacks (not related to nullsafe feature)
    'llvm/'
    'clang/include/clang/Support/Compiler.h'

    # lldb config
    'lldb/'
)

# get list of changed files relative to upstream
ALL_FILES=$(git diff --name-only "$BASE_REF"...HEAD)

# filter to upstream-worthy files
INCLUDE_FILES=()
for file in $ALL_FILES; do
    excluded=false
    for pattern in "${EXCLUDE_PATTERNS[@]}"; do
        case "$file" in
            $pattern*) excluded=true; break ;;
        esac
        # also handle glob-style patterns with fnmatch
        if [[ "$file" == $pattern ]]; then
            excluded=true
            break
        fi
    done
    if [[ "$excluded" == "false" ]]; then
        INCLUDE_FILES+=("$file")
    fi
done

echo "=== Files to include in upstream PR (${#INCLUDE_FILES[@]}) ==="
printf '%s\n' "${INCLUDE_FILES[@]}"
echo ""

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
  -fflow-sensitive-nullability    enables the analysis
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
