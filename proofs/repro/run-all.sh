#!/bin/sh
# Every finding that has a repro. Skips what it has no source tree for.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
for S in "$HERE"/[0-9]*.sh; do
  echo; sh "$S"
done
