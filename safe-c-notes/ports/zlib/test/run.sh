#!/bin/bash
# Builds stock zlib (Z_PREFIX, with the pristine inflate in orig/) and the
# port's inflate (inflate.c, inftrees.c and inffast.c under Safe C) into one
# program, then runs test/diff.c: run.sh [cases] (default 5000), or
# run.sh bench FILE. SAFEC picks the compiler, SEED the fuzz seed (default 1).
set -e
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
cc=${SAFEC:-$here/../../../build.noindex/bin/clang}
export SDKROOT=${SDKROOT:-$(xcrun --show-sdk-path)}
out=$(mktemp -d); mkdir -p "$out/stock" "$out/safe"
w="-O2 -DZLIB_CONST -Wno-nullability-completeness"
safe="-fsafe-c -mllvm -safe-c-ownership=false"
for f in adler32 crc32 zutil deflate trees compress uncompr; do
  "$cc" $w -DZ_PREFIX -c $f.c -o "$out/stock/$f.o"
done
for f in inflate inftrees inffast; do
  "$cc" $w -DZ_PREFIX -I. -c orig/$f.c -o "$out/stock/$f.o"
done
# (z_errmsg is not renamed by Z_PREFIX)
for f in adler32 crc32 zutil; do
  "$cc" $w -Dz_errmsg=safe_z_errmsg -c $f.c -o "$out/safe/$f.o"
done
for f in inflate inftrees inffast; do
  "$cc" $safe $w -Dz_errmsg=safe_z_errmsg -c $f.c -o "$out/safe/$f.o"
done
"$cc" -O2 -I. test/diff.c "$out"/stock/*.o "$out"/safe/*.o -o "$out/diff" 2>/dev/null
if [ "${1:-}" = bench ]; then "$out/diff" bench "$2"; exit; fi
"$out/diff" "${1:-5000}" "${SEED:-1}"
# the rest of the API (headers, dictionaries, inflateCopy, inflateSync)
"$cc" -O2 -w -I. test/api.c "$out"/stock/*.o "$out"/safe/*.o -o "$out/api"
"$out/api"
# CVE-2022-37434 (test/cve.c) against the port alone
"$cc" -O2 -w -I. test/cve.c "$out"/safe/*.o -o "$out/cve"
"$out/cve"
