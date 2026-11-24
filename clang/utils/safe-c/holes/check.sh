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
cp "$H/dfree.c" "$H"/void/*.c "$H"/frees/*.c .
total=0; holes=0
for f in *.c; do
  total=$((total + 1))
  $SC -fsafe-c ${SDK:+-isysroot $SDK} -fsyntax-only $f 2>$f.err || {
    grep -qE "Assertion|PLEASE submit" $f.err && { echo "CRASH $f"; holes=$((holes + 1)); }
    continue
  }
  /usr/bin/clang -w -fsanitize=address -I "$R/safe-c-std" -include "$R/safe-c-std/safec.h" $f -o $f.x 2>/dev/null || { echo "ASANBUILD $f"; holes=$((holes + 1)); continue; }
  printf 'AAAAAAAAAAAAAAAA' | perl -e 'alarm 10; exec @ARGV' ./$f.x >$f.out 2>&1
  # A forged pointer is a plain crash to ASan (DEADLYSIGNAL), also an error.
  grep -q "AddressSanitizer" $f.out || continue
  # v08 (bounds) and v12 (null) are stopped by Safe C at run time: they must
  # trap. Nothing else may, or a trap in malloc would hide a double free.
  case $f in v08.c|v12.c)
    $SC -fsafe-c ${SDK:+-isysroot $SDK} $f -o $f.sc 2>/dev/null
    ./$f.sc >/dev/null 2>&1; [ $? -eq 133 ] && continue ;;
  esac
  echo "HOLE $f"; holes=$((holes + 1))
done
echo "$total programs, $holes holes"
[ $total -gt 100 ] && [ $holes -eq 0 ]
