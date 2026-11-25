#!/bin/bash
# usage: run.sh p.c  -> compiles with Safe C; if accepted, runs both and prints classification
export ASAN_OPTIONS=symbolize=0:detect_leaks=0
root=$(cd "$(dirname "$0")/../../../.." && pwd)
f=$1; b=${f%.c}; case $b in /*) ;; *) b=./$b;; esac
SC=${SC:-$root/build.noindex/bin/clang}
SDK=$(xcrun --show-sdk-path 2>/dev/null)
/usr/bin/clang -w -fsanitize=address,undefined -fno-sanitize-recover=all -fwrapv -fno-sanitize=signed-integer-overflow -include $root/clang/lib/Headers/safec.h -O1 "$f" -o $b.asan 2>$b.asanerr || { echo "ORACLEBUILDFAIL $f: $(head -2 $b.asanerr)"; exit 0; }
if ! $SC -fsafe-c ${OWN--mllvm -safe-c-strict-ownership=true} -isysroot $SDK -O1 "$f" -o $b.sc 2>$b.scerr; then
  # Rejected: did the program have a bug ASan sees with this input?
  perl -e 'alarm 10; exec @ARGV' $b.asan >$b.asout 2>&1
  if grep -qE 'ERROR: AddressSanitizer|runtime error' $b.asout; then k=REJECT_BUG; else k=REJECT_CLEAN; fi
  echo "$k $(basename $f): $(grep -m1 error: $b.scerr | sed "s/.*error: //" | cut -c1-90)"; exit 0; fi
perl -e 'alarm 10; exec @ARGV' $b.sc >$b.scout 2>&1; rs=$?
perl -e 'alarm 10; exec @ARGV' $b.asan >$b.asout 2>&1; ra=$?
asanerr=$(grep -m1 -E 'ERROR: AddressSanitizer|runtime error' $b.asout)
if [ $rs -eq 133 ] || [ $rs -eq 132 ]; then cls=TRAP; elif [ -n "$asanerr" ]; then cls=HOLE; else cls=OK; fi
echo "$cls $(basename $f) sc=$rs asan=$ra :: $asanerr :: $(head -c 200 $b.scout | tr '\n' ' ')"
