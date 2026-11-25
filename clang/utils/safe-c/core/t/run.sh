# every ok program accepted, every bad program rejected by the checker (not by clang alone)
H=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); f=0
for k in ok bad; do mkdir $T/$k; awk -v d=$T/$k '/^\/\/\/\//{f=d"/"$2".c";next}{print > f}' $H/$k.txt
for p in $T/$k/*.c; do out=$(bun $H/../check.ts $p 2>&1); rc=$?
if { [ $k = ok ] && [ $rc != 0 ]; } || { [ $k = bad ] && [ $rc != 1 ]; }; then f=1; echo "WRONG $k $(basename $p) rc=$rc: $(echo "$out" | head -2 | cut -c1-200)"; elif [ -n "$V" ]; then echo "$k $(basename $p): $(echo "$out" | head -1 | cut -c1-150)"; fi; done; done; exit $f
