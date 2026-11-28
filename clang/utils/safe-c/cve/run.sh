#!/bin/bash
# Replays of CVE-class bugs from the Magma benchmark (v1.2), each the vulnerable logic written in Safe C by hand (README.md).
#  all.txt: the first 20 of the 80 patches of libpng, libtiff, libsndfile, lua, sqlite3 and libxml2, by sha1 of the patch's path.
#           The bad input must be rejected at compile time or trap (exit 133). (XML003, TIF014, SQL012 are not memory bugs.)
#  uaf.txt: every bug whose CVE is called a use after free (4). The bad version must be rejected at compile time.
# In both, the fixed version (-DGOOD) must compile and exit 0. Fails if any program fails, or if the count is not the expected one.
H=$(cd "$(dirname "$0")" && pwd); C=${SAFEC:-$H/../../../../build.noindex/bin/clang}; export SDKROOT=$(xcrun --show-sdk-path)
T=$(mktemp -d /tmp/cve.XXXXXX); bad=0; n=0
mkdir $T/all $T/uaf
awk -v d=$T/all 'BEGIN{h="#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdint.h>\n#ifdef GOOD\n#define V(a, b) (b)\n#else\n#define V(a, b) (a)\n#endif\n"} /^\/\/\/\//{f=d"/"$2".c"; printf "%s",h > f; next}{print > f}' "$H/all.txt"
awk -v d=$T/uaf 'BEGIN{h="#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#ifdef GOOD\n#undef GOOD\n#define GOOD 1\n#else\n#define GOOD 0\n#endif\n"} /^\/\/\/\//{f=d"/"$2".c"; printf "%s",h > f; next}{print > f}' "$H/uaf.txt"
good() { # file: the fixed version compiles and exits 0
  $C -fsafe-c -w -O2 -DGOOD "$1" -o $T/g 2>/dev/null && $T/g >/dev/null 2>&1
}
for f in $T/all/*.c $T/uaf/*.c; do
  [ -f "$f" ] || continue
  n=$((n + 1)); id=$(basename $f .c); kind=$(basename $(dirname $f))
  if ! $C -fsafe-c -w -O2 $f -o $T/b 2>$T/e; then v="REJECTED $id: $(grep -m1 error: $T/e | sed 's/.*error: //' | cut -c1-90)"
  elif [ $kind = uaf ]; then v="MISSED   $id: accepted"; bad=1
  else $T/b >/dev/null 2>&1; x=$?; [ $x = 133 ] && v="STOPPED  $id" || { v="MISSED   $id: bad input exit $x"; bad=1; }; fi
  if good $f; then echo "$v (good input runs)"; else echo "$v; FIXED VERSION FAILS"; bad=1; fi
done
echo "$n programs"
[ $n = 23 ] || { echo "expected 23 programs (18 in all.txt, 5 in uaf.txt)"; bad=1; }
exit $bad
