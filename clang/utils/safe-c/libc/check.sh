#!/bin/bash
# Every C99 library function in c99.txt either works in Safe C or is an error that says what to use instead.
# break/: misuse of the ones that work; judged by ../holes/blind3/judge.sh.
H=$(cd "$(dirname "$0")" && pwd); C=${SAFEC:-$H/../../../../build.noindex/bin/clang}; export SDKROOT=$(xcrun --show-sdk-path)
export CC_SAFEC=$C; T=$(mktemp -d /tmp/libc.XXXXXX); w=0; f=0; bad=0
while IFS='|' read -r n b; do
  { printf '#include <%s.h>\n' stdio stdlib string time ctype math signal locale setjmp wchar; printf 'static void h(void){}\nstatic int cmp(const void*a,const void*b){return 0;}\nvoid t(void){ %s }\n' "$b"; } > $T/$n.c
  e=$($C -fsafe-c -w -fsyntax-only $T/$n.c 2>&1 </dev/null | grep -m1 error: | sed 's/.*error: //')
  case "$e" in "") w=$((w+1)); [ "$1" = -v ] && echo "works   $n";; *"; use "*|*"Use "*) f=$((f+1)); [ "$1" = -v ] && echo "flagged $n: $e";; *) echo "NO RULE $n: $e"; bad=1;; esac
done < "$H/c99.txt"
echo "works $w, flagged $f"; [ $w -gt 80 ] || bad=1
bash "$H/../holes/blind3/judge.sh" "$H"/break/*.c 2>/dev/null </dev/null | grep -E "^(HOLE|CHEAT|CRASH)" && bad=1
exit $bad
