#!/bin/bash
# Every bug Magma v1.2 has in a C library whose CVE is called a use after free (4). The buggy version must be rejected and the fixed one accepted and run.
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../../../.." && pwd); C=${SAFEC:-$R/build.noindex/bin/clang}; export SDKROOT=$(xcrun --show-sdk-path) ASAN_OPTIONS=detect_leaks=0
T=$(mktemp -d /tmp/uaf.XXXXXX); bad=0
awk -v d=$T 'BEGIN{h="#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#ifndef GOOD\n#define GOOD 0\n#endif\n"} /^\/\/\/\//{f=d"/"$2".c"; printf "%s",h > f; next}{print > f}' "$H/uaf.txt"
for f in $T/*.c; do n=$(basename $f .c); e=$($C -fsafe-c -w -fsyntax-only $f 2>&1 | grep -m1 "error:" | sed 's/.*error: //')
 /usr/bin/clang -w -g -fsanitize=address -I$R/safe-c-std -include $R/safe-c-std/safec.h $f -o $T/a && a=$($T/a 2>&1 | grep -aom1 "AddressSanitizer: [a-z-]*")
 g=$($C -fsafe-c -w -DGOOD=1 $f -o $T/g 2>&1 | grep -m1 "error:"); if [ -z "$g" ]; then $T/g >/dev/null 2>&1; g="fixed version runs ($?)"; fi
 [ -n "$e" ] && echo "REJECTED $n: ${e:0:90} | plain C: ${a:-no error seen} | $g" || { echo "MISSED $n | $g"; bad=1; }; done; exit $bad
