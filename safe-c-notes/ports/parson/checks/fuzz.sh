set -e
R=$HOME/git/safe-c-parson; cd "$(dirname "$0")"; mkdir -p base; git -C $R show f151425:parson/parson.c > base/parson.c; git -C $R show f151425:parson/parson.h > base/parson.h; cp $R/parson/safec.h base/
CF="-g -w -O1 -fsanitize=address,undefined -fno-sanitize-recover=all"
/usr/bin/clang $CF -I$R/parson fuzz.c $R/parson/parson.c -o new; /usr/bin/clang $CF -DBASE -Ibase fuzz.c base/parson.c -o old
export SDKROOT=$(xcrun --show-sdk-path); C=~/git/llvm-safe-c/build.snap17/bin/clang; $C -fsafe-c -w -O1 -mllvm -safe-c-ownership=false -c $R/parson/parson.c -o p.o; $C -O1 -w -I$R/parson fuzz.c p.o -o newsafe
set +e
for b in new old newsafe; do ./$b 300 2000 > $b.txt 2>&1; tail -1 $b.txt; done; cmp new.txt old.txt && cmp newsafe.txt old.txt && echo same trace
for f in 5 30 300; do for b in new newsafe; do echo "$b failing 1/$f: $(./$b 200 2000 $f 2>&1 | tail -1)"; done; done
