#!/bin/sh
# The fast gates for -fsemantic-deps, clang-semindex and semninja: the lit
# tests and the mutation fuzzer. Seconds, not the minutes a real build takes.
# usage: check.sh <build dir>
set -e
B=$(cd "${1:?usage: check.sh <build dir>}" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
ninja -C "$B" clang clang-semindex FileCheck split-file not count llvm-config > /dev/null
"$B/bin/llvm-lit" -q "$HERE/../../test/Frontend/semantic-deps.cpp" \
  "$HERE/../../test/Tooling/clang-semindex-graph.cpp"
bun "$HERE/fuzz/fuzz.ts" "$B/bin" | tail -1
bun "$HERE/fuzz/fuzz.ts" "$B/bin" --random 100 1 | tail -1
bun "$HERE/fuzz/semninja-test.ts" "$B/bin" | tail -1
