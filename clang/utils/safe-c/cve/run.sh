#!/bin/bash
# Replays of CVE-class bugs from the Magma benchmark (v1.2): the first 20 of the 80 patches of libpng, libtiff, libsndfile, lua,
# sqlite3 and libxml2, by sha1 of the patch's path. Each is the vulnerable logic, written in Safe C by hand. For each: what
# Safe C does with the bad input, and that the same program runs with a good one. (XML003, TIF014 and SQL012 are not memory bugs.)
H=$(cd "$(dirname "$0")" && pwd); C=${SAFEC:-$H/../../../../build.noindex/bin/clang}; export SDKROOT=$(xcrun --show-sdk-path)
T=$(mktemp -d /tmp/cve.XXXXXX); bad=0
awk -v d=$T 'BEGIN{h="#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdint.h>\n#ifdef GOOD\n#define V(a, b) (b)\n#else\n#define V(a, b) (a)\n#endif\n"} /^\/\/\/\//{f=d"/"$2".c"; printf "%s",h > f; next}{print > f}' "$H/all.txt"
for f in $T/*.c; do n=$(basename $f .c)
 if ! $C -fsafe-c -w -O2 $f -o $T/b 2>$T/e; then echo "REJECTED $n: $(grep -m1 error: $T/e | sed 's/.*error: //' | cut -c1-90)"; continue; fi
 $T/b >/dev/null 2>&1; x=$?; $C -fsafe-c -w -O2 -DGOOD $f -o $T/g 2>/dev/null; $T/g >/dev/null 2>&1; y=$?
 if [ $x = 133 ] && [ $y = 0 ]; then echo "STOPPED  $n (good input runs)"; else echo "MISSED   $n bad=$x good=$y"; bad=1; fi; done 2>/dev/null
exit $bad
