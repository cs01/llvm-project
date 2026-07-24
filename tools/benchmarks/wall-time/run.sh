#!/bin/bash
# Microbenchmarks: baseline vs nullsafe vs analyzer on (1) pointer-dense synthetic C
# and (2) a realistic STL-heavy C++ TU. Uses hyperfine + taskset for stable timing.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLANG="${CLANG:-$here/../../../build/bin/clang}"
PIN="${PIN:-taskset -c 4}"
NS="-fflow-sensitive-nullability -fnullability-default=nullable"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

# (1) synthetic pointer-dense C
python3 "$here/gen_synth.py" 2500 > "$tmp/synth.c"
# (2) realistic C++ preprocessed to a deterministic .ii
cat > "$tmp/real.cpp" <<'EOF'
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <algorithm>
struct Widget { int id; std::string name; std::unique_ptr<Widget> child; };
static int process(const std::vector<std::unique_ptr<Widget>>& ws,
                   std::unordered_map<std::string,int>& idx) {
    int total = 0;
    for (const auto& w : ws) { if (!w) continue; idx[w->name]=w->id;
        if (w->child) total += w->child->id; total += w->id; }
    std::vector<int> ids; for (auto& kv : idx) ids.push_back(kv.second);
    std::sort(ids.begin(), ids.end());
    return total + (ids.empty()?0:ids.front());
}
int main(){ std::vector<std::unique_ptr<Widget>> v; std::unordered_map<std::string,int> m; return process(v,m); }
EOF
"$CLANG" -std=c++17 -E "$tmp/real.cpp" -o "$tmp/real.ii"

echo "===== GROUP A: synthetic pointer-dense C, -fsyntax-only (worst case) ====="
$PIN hyperfine --shell=none --warmup 5 --runs 50 --export-json "$here/A_synth.json" \
  -n baseline "$CLANG -fsyntax-only $tmp/synth.c" \
  -n nullsafe "$CLANG -fsyntax-only $NS $tmp/synth.c" \
  -n analyzer "$CLANG --analyze -Xclang -analyzer-output=text $tmp/synth.c"

echo "===== GROUP B: realistic STL-heavy C++ (.ii), -fsyntax-only ====="
$PIN hyperfine --shell=none --warmup 5 --runs 40 --export-json "$here/B_real.json" \
  -n baseline "$CLANG -std=c++17 -fsyntax-only $tmp/real.ii" \
  -n nullsafe "$CLANG -std=c++17 -fsyntax-only $NS $tmp/real.ii" \
  -n analyzer "$CLANG -std=c++17 --analyze -Xclang -analyzer-output=text $tmp/real.ii"
