#!/bin/bash
# Programs that once got past the compiler (a/b/c.txt: the Lean out-argument
# work, 2026-11-24; sections "//// name", pre.h prepended) and dfree.c. Prints
# each one still accepted that ASan flags; exits 1 if there are any, or if no
# program was judged at all.
export ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=0:symbolize=0
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../../../.." && pwd)
SC=${SC:-$R/build.noindex/bin/clang}; SDK=$(xcrun --show-sdk-path 2>/dev/null)
T=$(mktemp -d); cd "$T" || exit 1
for x in a b c; do
  awk -v pre="$H/pre.h" '/^\/\/\/\//{f=$2".c"; while((getline l < pre)>0) print l > f; close(pre); next}{print > f}' "$H/$x.txt"
done
cp "$H/dfree.c" .
total=0; holes=0
for f in *.c; do
  total=$((total + 1))
  $SC -fsafe-c ${SDK:+-isysroot $SDK} -fsyntax-only $f 2>$f.err || {
    grep -qE "Assertion|PLEASE submit" $f.err && { echo "CRASH $f"; holes=$((holes + 1)); }
    continue
  }
  /usr/bin/clang -w -fsanitize=address -include "$R/clang/lib/Headers/safec.h" $f -o $f.x 2>/dev/null || { echo "ASANBUILD $f"; continue; }
  perl -e 'alarm 10; exec @ARGV' ./$f.x >$f.out 2>&1
  grep -q "ERROR: AddressSanitizer" $f.out && { echo "HOLE $f"; holes=$((holes + 1)); }
done
echo "$total programs, $holes holes"
[ $total -gt 100 ] && [ $holes -eq 0 ]
