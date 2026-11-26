#!/bin/bash
# The port against the original lwrb (original/): random operations, same output.
H=$(cd "$(dirname "$0")" && pwd); R=$H/../../..; C=${SAFEC:-$R/build.noindex/bin/clang}; export SDKROOT=$(xcrun --show-sdk-path)
T=$(mktemp -d /tmp/lwrb.XXXXXX)
/usr/bin/clang -w -O1 -I"$H/original/lwrb" "$H/harness.c" "$H/original/lwrb.c" -o $T/original || exit 1
"$C" -fsafe-c -O1 -I"$H" -c "$H/lwrb.c" -o $T/port.o || exit 1
/usr/bin/clang -w -O1 -I"$H" "$H/harness.c" $T/port.o -o $T/port || exit 1
same=0; bad=0
for s in $(seq 1 ${1:-300}); do
  $T/original $s > $T/a 2>&1; x=$?; $T/port $s > $T/b 2>&1; y=$?
  if [ $x = $y ] && cmp -s $T/a $T/b; then same=$((same+1)); else bad=$((bad+1)); [ $bad -le 2 ] && { echo "seed $s: original $x, port $y"; diff $T/a $T/b | head -4; }; fi
done
echo "lwrb: $same same, $bad different, of $((same+bad)) runs of 3,000 operations"
[ $bad = 0 ]
