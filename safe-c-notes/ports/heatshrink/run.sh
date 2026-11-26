#!/bin/bash
# The port against the original heatshrink (original/), static mode, both directions.
H=$(cd "$(dirname "$0")" && pwd); R=$H/../../..; C=${SAFEC:-$R/build.noindex/bin/clang}; export SDKROOT=$(xcrun --show-sdk-path)
T=$(mktemp -d /tmp/heatshrink.XXXXXX)
/usr/bin/clang -w -O1 -I"$H/original" "$H/harness.c" "$H/original/heatshrink_decoder.c" "$H/original/heatshrink_encoder.c" -o $T/original || exit 1
"$C" -fsafe-c -O1 -I"$H" -c "$H/heatshrink_decoder.c" -o $T/d.o && "$C" -fsafe-c -O1 -I"$H" -c "$H/heatshrink_encoder.c" -o $T/e.o || exit 1
/usr/bin/clang -w -O1 -I"$H" "$H/harness.c" $T/d.o $T/e.o -o $T/port || exit 1
bun "$H/fuzz.ts" $T/original $T/port ${1:-400}
