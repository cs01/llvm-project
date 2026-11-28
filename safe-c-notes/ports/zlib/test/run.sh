#!/bin/bash
# Builds stock zlib (Z_PREFIX, with orig/inffast.c) and the port's inflate into
# one program, then runs test/diff.c: run.sh [cases] (default 5000), or
# run.sh bench FILE. SAFEC picks the compiler.
set -e
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
cc=${SAFEC:-$here/../../../build.noindex/bin/clang}
export SDKROOT=${SDKROOT:-$(xcrun --show-sdk-path)}
out=$(mktemp -d); mkdir -p "$out/stock" "$out/safe"
w="-O2 -DZLIB_CONST -Wno-nullability-completeness"
for f in adler32 crc32 inflate inftrees zutil deflate trees compress uncompr; do
  "$cc" $w -DZ_PREFIX -c $f.c -o "$out/stock/$f.o"
done
"$cc" $w -DZ_PREFIX -I. -c orig/inffast.c -o "$out/stock/inffast.o"
# (z_errmsg is not renamed by Z_PREFIX)
for f in adler32 crc32 inflate inftrees zutil; do
  "$cc" $w -Dz_errmsg=safe_z_errmsg -c $f.c -o "$out/safe/$f.o"
done
"$cc" -fsafe-c -mllvm -safe-c-ownership=false $w -Dz_errmsg=safe_z_errmsg -c inffast.c -o "$out/safe/inffast.o"
"$cc" -O2 -I. test/diff.c "$out"/stock/*.o "$out"/safe/*.o -o "$out/diff" 2>/dev/null
if [ "${1:-}" = bench ]; then "$out/diff" bench "$2"; else "$out/diff" "${1:-5000}" 1; fi
