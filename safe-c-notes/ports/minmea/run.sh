#!/bin/bash
# The port against the original minmea (original/), on 50,000 generated GPS sentences.
H=$(cd "$(dirname "$0")" && pwd); R=$H/../../..; C=${SAFEC:-$R/build.noindex/bin/clang}; export SDKROOT=$(xcrun --show-sdk-path)
T=$(mktemp -d /tmp/minmea.XXXXXX)
/usr/bin/clang -w -O1 -I"$H/original" "$H/harness.c" "$H/original/minmea.c" -o $T/original || exit 1
"$C" -fsafe-c -O1 -I"$H" -c "$H/minmea.c" -o $T/port.o || exit 1
/usr/bin/clang -w -O1 -I"$H" "$H/harness.c" $T/port.o -o $T/port || exit 1
bun "$H/fuzz.ts" $T/original $T/port ${1:-50000}
