# clangjudge.sh FILE...: verdict of the checker inside clang; exit 0 accepted, 1 rejected, 2 crashed (one file)
H=$(cd "$(dirname "$0")" && pwd); C=${CLANG:-$(cd "$H/../../../.." && pwd)/build.noindex/bin/clang}; SDK=$(xcrun --show-sdk-path); r=0
for f in "$@"; do a=$($C -### -fsafe-c -w -include $H/core.h -isysroot $SDK -c "$f" -o /dev/null 2>&1 | tail -1 | sed 's/"-emit-obj"/"-fsyntax-only"/; s/"-o" "[^"]*"//'); eval "$a -mllvm -safe-c-ownership=false -mllvm -safe-c-ownership-ir=true" >${OUT:-/tmp/cj.$$} 2>&1; q=$?
 o=$(grep -m1 "error:" ${OUT:-/tmp/cj.$$} | sed 's/.*error: //'); grep -qE "Assertion|PLEASE submit|UNREACHABLE" ${OUT:-/tmp/cj.$$} && { echo "CRASH $(basename $f) $(grep -m1 -E 'Assertion|UNREACH' ${OUT:-/tmp/cj.$$} | cut -c1-120)"; r=2; continue; }
 [ $q = 0 ] && { echo "ACCEPT $(basename $f)"; r=0; } || { echo "REJECT $(basename $f): $o"; r=1; }; done; rm -f /tmp/cj.$$; exit $r
