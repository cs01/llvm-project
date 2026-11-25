P=clang/utils/safe-c/ir-proto; R=$PWD; O=$D/bad; H=clang/utils/safe-c/holes; rm -rf $O; mkdir -p $O/src; export ASAN_OPTIONS=detect_leaks=0:symbolize=0:detect_stack_use_after_return=1
bun -e 'import{readFileSync as r,writeFileSync as w,readdirSync as d}from"fs";const H=process.argv[1],O=process.argv[2];
const pre1=r(H+"/pre.h","utf8");const pre2="#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdint.h>\n";
const ex=(f,t,pre)=>{for(const s of r(f,"utf8").split(/^\/\/\/\/ /m).slice(1)){const nl=s.indexOf("\n");const n=s.slice(0,nl).split(" ")[0];if(s.includes("//// file2")||/two|tsan/.test(s.slice(0,nl))||n=="file2")continue;w(O+"/src/"+t+"_"+n+".c",pre+s.slice(nl+1));}};
for(const x of["a","b","c"])ex(H+"/"+x+".txt","h"+x,pre1);
for(const f of d(H+"/blind"))if(f.endsWith(".txt"))ex(H+"/blind/"+f,"b1"+f.replace(".txt",""),pre2);' $H $O
for f in $(find $H/void $H/frees $H/tags $H/juliet $H/blind2 -name '*.c') $H/dfree.c; do cp $f $O/src/$(echo $f | sed "s|$H/||; s|/|_|g"); done
for f in $O/src/*.c; do b=$(basename $f .c)
 /usr/bin/clang -w -fsanitize=address -I $R/safe-c-std -include $R/safe-c-std/safec.h $f -o $O/x 2>/dev/null </dev/null || { echo "$b nocompile"; continue; }
 k=$(printf AAAAAAAAAAAAAAAA | perl -e 'alarm 10; exec @ARGV' $O/x 2>&1 | grep -m1 -oE "heap-use-after-free|double-free|attempting free on address|stack-use-after[a-z-]*|[a-z]+-buffer-overflow|SEGV"); echo "$b ${k:-none}"
 case "$k" in heap-use-after-free*|double*|attempting*) bash $P/lower.sh $f $O/$b.flat -isystem $R/safe-c-std -include $R/safe-c-std/safec.h || bash $P/lower.sh $f $O/$b.flat -fsafe-c || echo "$b notlowered";; esac
done > $O/kinds 2>/dev/null
awk '{$1="";print}' $O/kinds | sort | uniq -c
