#!/bin/sh
# FINDINGS: expat storeRawNames and two sqlite sites read a pointer that realloc
# has already freed. This is the detector those came from.
# Write-ups: ../generalize/expat/, ../generalize/sqlite/
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
echo "== realloc-aliasing scan: a pointer read after realloc freed it =="
FOUND=0
for T in "${EXPAT:-$HOME/expat}" "${SQLITE:-$HOME/sqlite}" "${ZLIB:-$HOME/zlib}" \
         "${ZSTD:-$HOME/facebook/zstd}" "${REDIS:-$HOME/redis}"; do
  [ -d "$T" ] || continue
  FOUND=1
  echo "-- $T"
  python3 "$HERE/../scan-realloc-aliasing.py" "$T" 2>/dev/null | sed 's/^/   /'
done
[ "$FOUND" -eq 1 ] || echo "  SKIP: no source trees found; set EXPAT/SQLITE/ZLIB/ZSTD/REDIS"
echo "  (text-level triage, deliberately noisy -- confirm every hit by reading it)"
