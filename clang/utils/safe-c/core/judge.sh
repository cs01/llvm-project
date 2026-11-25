# judge.sh FILE.c...: prints VERDICT. HOLE = accepted by the checker, and wrong when run.
export ASAN_OPTIONS=detect_leaks=0:symbolize=0:detect_stack_use_after_return=1
H=$(cd "$(dirname "$0")/.." && pwd); R=$(cd "$H/../../.." && pwd); export SC_BUILD=$R/build.noindex
for f in "$@"; do f=$(cd "$(dirname "$f")" && pwd)/$(basename "$f"); out=$(perl -e 'alarm 60; exec @ARGV' bun $H/core/check.ts "$f" 2>&1); rc=$?
 [ $rc = 1 ] && { echo "REJECT $(basename $f): $(echo "$out" | head -1 | cut -c1-140)"; continue; }
 [ $rc = 0 ] || { echo "CHECKER-FAILED($rc) $(basename $f)"; continue; }
 t=$(mktemp -d); /usr/bin/clang -w -g -fsanitize=address -I $R/safe-c-std -include $R/safe-c-std/safec.h -D'_Taken_if_true=' -D'_Taken_if_zero=' "$f" -o $t/a 2>$t/e || { echo "NOASAN $(basename $f): $(grep -m1 error: $t/e)"; continue; }
 k=$(perl -e 'alarm 10; exec @ARGV' $t/a 2>&1 </dev/null | grep -am1 -oE "heap-use-after-free|double-free|attempting free[a-z -]*|stack-use-after-[a-z]*|AddressSanitizer: [a-zA-Z-]+"); [ -z "$k" ] && { echo "ACCEPT-OK $(basename $f)"; continue; }
 $R/build.noindex/bin/clang -fsafe-c -w -include $H/core/core.h -isysroot $(xcrun --show-sdk-path) "$f" -o $t/b 2>/dev/null && perl -e 'alarm 10; exec @ARGV' $t/b >/dev/null 2>&1 </dev/null; e=$?; echo "HOLE $(basename $f): $k (safe-c exit $e)"; done
