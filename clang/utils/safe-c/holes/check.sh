#!/bin/bash
# Programs that once got past the compiler (a/b/c.txt: the Lean out-argument
# work, 2026-11-24; sections "//// name", pre.h prepended) and dfree.c, void/,
# frees/, juliet/, tags/. Each is judged by blind3/judge.sh, the same judge as
# the attacks: a HOLE, CRASH or NOASAN verdict fails, and so does judging
# fewer than 100 programs.
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../../../.." && pwd)
export CC_SAFEC=${SC:-${SAFEC:-$R/build.noindex/bin/clang}}
T=$(mktemp -d); cd "$T" || exit 1
for x in a b c; do
  awk -v pre="$H/pre.h" '/^\/\/\/\//{f=$2".c"; while((getline l < pre)>0) print l > f; close(pre); next}{print > f}' "$H/$x.txt"
done
cp "$H/dfree.c" "$H"/void/*.c "$H"/frees/*.c "$H"/juliet/*.c "$H"/tags/*.c .
# Marks some of these programs use that Safe C no longer has.
export JUDGE_FLAGS='-D_Taken_if_true= -D_Taken_if_zero= -D_Exclusive= -D_View= -D_Unchecked= -D_Returns_owned= -D_Lifetimebound= -D_Unsynchronized= -D_Takes(N)= -D_Drop(f)=__attribute__((cleanup(f)))'
out=$(bash "$H/blind3/judge.sh" *.c 2>/dev/null </dev/null)
echo "$out" | grep -E "^(HOLE|CRASH|NOASAN|CHEAT)"
total=$(echo "$out" | grep -c .); holes=$(echo "$out" | grep -cE "^(HOLE|CRASH|NOASAN|CHEAT)")
echo "$total programs, $holes holes"
[ "$total" -gt 100 ] && [ "$holes" -eq 0 ]
