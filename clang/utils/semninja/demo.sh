#!/bin/zsh
# A/B on a real commit: rebuild after reverting its header changes with
# semninja (verifying every kept object), then after re-applying them with
# plain ninja. Leaves the tree as it found it.
#
# usage: demo.sh <builddir> <commit> [clang-semindex]
set -eu
B=${1:a}; C=$2; I=${3:-$B/bin/clang-semindex}
HERE=${0:a:h}
SRC=$(git -C $B/.. rev-parse --show-toplevel)
PATCH=$(mktemp)
trap 'rm -f $PATCH' EXIT
# The whole commit: its .cpp changes usually need its header changes.
git -C $SRC show --format= $C > $PATCH
git -C $SRC apply -R --check $PATCH || { echo "$C does not revert cleanly"; exit 1; }

echo "== $(git -C $SRC show -s --format='%h %s' $C)"
ninja -C $B clang > /dev/null || { echo "initial build FAILED"; exit 1; }
bun $HERE/semninja.ts -C $B --indexer $I clang > /dev/null 2>&1   # snapshot

now() { gdate +%s.%N 2>/dev/null || date +%s; }
git -C $SRC apply -R $PATCH
# Both timed builds start with an empty codegen cache, so neither is helped
# by objects the other already produced.
rm -rf $B/codegen-cache
s=$(now)
bun $HERE/semninja.ts -C $B --indexer $I clang > $PATCH.log 2>&1 || { tail -20 $PATCH.log; echo "semninja BUILD FAILED"; git -C $SRC apply $PATCH; exit 1; }
grep -E 'skipping|done' $PATCH.log
t1=$(echo "$(now) - $s" | bc)
bun $HERE/semninja.ts -C $B --verify-last 2>&1 | grep verify

git -C $SRC apply $PATCH
rm -rf $B/codegen-cache
s=$(now)
ninja -C $B clang > /dev/null || { echo "ninja BUILD FAILED"; exit 1; }
t2=$(echo "$(now) - $s" | bc)
bun $HERE/semninja.ts -C $B --indexer $I clang > /dev/null 2>&1   # snapshot

printf "\n  plain ninja  %6.1fs\n  semninja     %6.1fs  (includes its analysis)\n  speedup      %6.1fx\n" $t2 $t1 $(echo "$t2 / $t1" | bc -l)
