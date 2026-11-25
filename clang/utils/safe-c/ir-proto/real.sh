P=clang/utils/safe-c/ir-proto; R=~/git/safec-census/src; mkdir -p $D/real
go() { n=$1; f=$2; shift 2; [ -f $f ] || { echo "$n missing"; return; }; perl -e 'alarm 120; exec @ARGV' bash $P/lower.sh $f $D/real/$n.flat "$@" || { echo "$n not lowered: $(grep -m1 -oE 'error: .{0,80}' $D/real/$n.flat.err)"; return; }
 printf "%-14s lines %6s " $n $(wc -l <$f); for l in all type root both; do printf "$l %s  " "$(perl -e 'alarm 120; exec @ARGV' bun $P/uaf.ts $l $D/real/$n.flat 2>/dev/null | awk '{print $3}' | tail -1)"; done; echo; }
go parson $D/parson.c
go sds $R/sds/sds.c
go ini $R/inih/ini.c
git show HEAD:safe-c-notes/ports/cjson/cJSON.c >/dev/null 2>&1
go cjson-port safe-c-notes/ports/cjson/cJSON.c -Isafe-c-notes/ports/cjson -fsafe-c
for f in $R/cmark/src/{blocks,inlines,node,buffer,references,iterator}.c; do go cmark-$(basename $f .c) $f -I$R/cmark/src -I$R/cmark/build/src; done
for f in $R/lua/{ltable,lstring,lgc,lparser,lapi,ldo,lauxlib}.c $R/lua/src/{ltable,lgc,lparser}.c; do [ -f $f ] && go lua-$(basename $f .c) $f; done
for f in $R/libyaml/src/{api,parser,scanner,emitter,loader,dumper}.c; do go yaml-$(basename $f .c) $f -I$R/libyaml/include -I$R/libyaml/src -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5 -DYAML_VERSION_STRING='"0.2.5"'; done
for f in $R/zlib/{deflate,inflate,gzread,gzwrite}.c; do go zlib-$(basename $f .c) $f; done
for f in $R/redis/src/{adlist,dict,listpack,rax,t_list}.c; do go redis-$(basename $f .c) $f -I$R/redis/src -I$R/redis/deps/lua/src -I$R/redis/deps/hiredis; done
