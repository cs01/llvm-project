C=${CLANG:?}; SDK=$(xcrun --show-sdk-path); cd ${MANY:?}; : > res.tsv; : > err.txt; : > crash.txt; mkdir -p out
for d in src/*/; do d=${d%/}; n=$(basename $d)
 I=$(find "$d" -name '*.h' -not -path '*/.git/*' 2>/dev/null | xargs -n1 dirname 2>/dev/null | sort -u | head -40 | sed 's/^/-I/' | tr '\n' ' '); tot=0
 for f in $(find "$d" -name '*.c' -not -path '*/.git/*' -size -600k | grep -viE '/(test|tests|testing|example|examples|third_party|thirdparty|3rdparty|vendor|deps|external|contrib|demo|bench|fuzz)[^/]*/' | while read x; do echo "$(printf %s "$x" | shasum | cut -c1-8) $x"; done | sort | head -12 | cut -d' ' -f2-); do
  tot=$((tot+1)); perl -e 'alarm 30; exec @ARGV' $C -w -std=gnu11 -fsyntax-only -isysroot $SDK $I "$f" >/dev/null 2>&1 || { printf '%s\t%s\t0\t0\tskip\n' "$n" "$f" >> res.tsv; continue; }
  l=$(wc -l < "$f"); perl -e 'alarm 60; exec @ARGV' $C -fsafe-c -w -std=gnu11 -ferror-limit=0 -isysroot $SDK $I -fsyntax-only "$f" > out/e 2>&1; r=$?
  if grep -qE 'Assertion|PLEASE submit' out/e; then s=crash; echo "$f $(grep -m1 Assertion out/e | cut -c1-120)" >> crash.txt; elif [ $r = 142 ]; then s=timeout; else s=ok; fi
  e=$(grep -cE "^$f:[0-9]+:[0-9]+: (fatal )?error:" out/e); h=$(grep -cE ': (fatal )?error:' out/e); [ $s = ok ] && [ $r != 0 ] && [ $h = 0 ] && s=odd; printf '%s\t%s\t%d\t%d\t%s\t%d\n' "$n" "$f" $l $e $s $((h-e)) >> res.tsv; grep -hE ': (fatal )?error:' out/e | sed -E 's/.*error: //' >> err.txt
 done; done
