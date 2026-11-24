#!/bin/bash
# usage: fuzz.sh START COUNT [tag]  -> runs seeds START..START+COUNT-1, 4 wide; appends to log-<tag>.txt
D=$(cd $(dirname $0) && pwd); T=${3:-a}; W=${FUZZ_OUT:-${TMPDIR:-/tmp}/safe-c-fuzz}; mkdir -p $W/w$T
one() { s=$1; f=$W/w$T/p$s.c; bun $D/gen.ts $s $f; r=$($D/run.sh $f 2>/dev/null); case $r in HOLE*|TRAP*) ;; *) rm -f $W/w$T/p$s.*;; esac; echo "$r"; }
export -f one; export D T W
seq $1 $(($1+$2-1)) | xargs -P4 -I{} bash -c 'one {}' >> $W/log-$T.txt
cut -d' ' -f1 $W/log-$T.txt | sort | uniq -c
# A run in which nearly nothing compiles proves nothing, and neither does one
# that stopped early.
ok=$(grep -c '^OK' $W/log-$T.txt); holes=$(grep -c '^HOLE' $W/log-$T.txt)
[ "$ok" -ge $(($2 / 20)) ] || { echo "VACUOUS: only $ok programs accepted"; exit 1; }
[ "$holes" -eq 0 ]
