#!/bin/bash
# judge.sh FILE.c...: one verdict per program.
#  HOLE      Safe C accepts it, the plain build has a memory error under ASan/UBSan, and the Safe C build does not trap
#  STOPPED   accepted, memory error in the plain build, the Safe C build traps at -O0 and -O2
#  ACCEPT-OK accepted, no error seen   REJECT error text   CRASH compiler crash   CHEAT uses an excluded construct
export SDKROOT=$(xcrun --show-sdk-path) ASAN_OPTIONS=detect_leaks=0:symbolize=0:detect_stack_use_after_return=1 UBSAN_OPTIONS=symbolize=0
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../../../../.." && pwd); C=${CC_SAFEC:-$(cd "$H/../../../../.." && pwd)/build.noindex/bin/clang}
run() { perl -e 'alarm 10; exec @ARGV' "$@" </dev/null >/dev/null 2>&1; }
for f in "$@"; do b=$(basename "$f")
 grep -qE '__unsafe|_Trusted|safe_c legacy|system_header|asm|pthread|setjmp|longjmp|safec_|clang::|__attribute__|fork|signal' "$f" && { echo "CHEAT $b"; continue; }
 t=$(mktemp -d /tmp/j4.XXXXXX); $C -fsafe-c -w -I"$R/safe-c-std" "$f" -o $t/s 2>$t/e; r=$?
 if [ $r != 0 ]; then grep -qE "Assertion|PLEASE submit|UNREACHABLE" $t/e && echo "CRASH $b" || echo "REJECT $b: $(grep -m1 error: $t/e | sed 's/.*error: //' | cut -c1-150)"; continue; fi
 $C -fsafe-c -w -O2 -I"$R/safe-c-std" "$f" -o $t/s2 2>/dev/null; run $t/s; x=$?; run $t/s2; y=$?
 /usr/bin/clang -w -g -ftrivial-auto-var-init=zero -fwrapv -fsanitize=address,null,bounds,object-size,alignment,vla-bound -fno-sanitize-recover=all -I"$R/safe-c-std" -include "$R/safe-c-std/safec.h" "$f" -o $t/a 2>$t/e2 || { echo "NOASAN $b: $(grep -m1 error: $t/e2 | cut -c1-120)"; continue; }
 k=$(perl -e 'alarm 20; exec @ARGV' $t/a 2>&1 </dev/null | grep -am1 -oE "AddressSanitizer: [a-zA-Z-]+|runtime error: [a-z A-Z-]+")
 tr() { [ $1 = 133 -o $1 = 132 -o $1 = 134 ]; }
 # The order of evaluation in the Safe C build is its own: catch what ASan on the plain build cannot.
 if [ -z "$k" ]; then for e in s s2; do perl -e 'alarm 20; exec @ARGV' /usr/bin/env DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib $t/$e </dev/null >/dev/null 2>&1; [ $? = 139 ] && k="guard malloc"; done; fi
 if [ -z "$k" ]; then echo "ACCEPT-OK $b O0=$x O2=$y"; elif tr $x && tr $y; then echo "STOPPED $b: $k"; else echo "HOLE $b: $k O0=$x O2=$y"; fi; done
