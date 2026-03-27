#!/usr/bin/env python3
"""Cross-analysis benchmark on real-world C++ code (v2).

Measures the real cost each analysis adds to compilation. Uses -fsyntax-only
to isolate frontend overhead from codegen.

Key design: all configs use the SAME base flags (the original compile command
minus PCH/dep-file/Werror), so we measure only the incremental cost of each
analysis.

Configs:
  1. Baseline (-w): no analysis at all
  2. Default: compile with default warnings (what you get today)
  3. + -Wuninitialized
  4. + -Wthread-safety
  5. + nullsafe (flow-sensitive nullability with default=nullable)
  6. CSA: --analyze (separate pass, measured separately, added to baseline)

For CSA, we measure the analyze-only time and report the total
(baseline + CSA) since that's what users would actually experience.
"""

import json
import os
import subprocess
import sys
import time
import math
import argparse
import shlex


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--compile-commands", default="build/compile_commands.json")
    p.add_argument("--clang-binary", default="build/bin/clang")
    p.add_argument("--num-files", type=int, default=10)
    p.add_argument("--warmup", type=int, default=1)
    p.add_argument("--iterations", type=int, default=3)
    p.add_argument("--filter", default="clang/lib")
    p.add_argument("--skip-csa", action="store_true")
    return p.parse_args()


def clean_compile_command(parts, clang_abs):
    """Strip PCH, dep files, -Werror. Keep -c and -o."""
    parts = list(parts)

    if parts[0].endswith("clang++") or parts[0].endswith("clang++-23"):
        parts[0] = clang_abs + "++"
    elif parts[0].endswith("clang") or parts[0].endswith("clang-23"):
        parts[0] = clang_abs

    parts = [p for p in parts if not p.startswith("-Werror")]

    # Strip PCH
    cleaned = []
    i = 0
    while i < len(parts):
        if (parts[i] == "-Xclang" and i + 3 < len(parts)
            and parts[i+1] == "-include-pch" and parts[i+2] == "-Xclang"):
            i += 4; continue
        if (parts[i] == "-Xclang" and i + 3 < len(parts)
            and parts[i+1] == "-include" and parts[i+2] == "-Xclang"
            and ("pch" in parts[i+3].lower() or parts[i+3].endswith(".hxx"))):
            i += 4; continue
        if parts[i] == "-Xclang" and i+1 < len(parts) and parts[i+1] == "-fno-pch-timestamp":
            i += 2; continue
        if parts[i] == "-Winvalid-pch":
            i += 1; continue
        cleaned.append(parts[i])
        i += 1
    parts = cleaned

    # Strip dep file flags
    dep_cleaned = []
    skip = False
    for p in parts:
        if skip: skip = False; continue
        if p in ("-MF", "-MT", "-MQ"): skip = True; continue
        if p in ("-MD", "-MMD"): continue
        dep_cleaned.append(p)
    parts = dep_cleaned

    # Use -fsyntax-only: remove -c, replace -o target
    new_parts = []
    skip = False
    for p in parts:
        if skip: skip = False; continue
        if p == "-o": skip = True; continue
        if p == "-c": continue
        new_parts.append(p)
    new_parts.append("-fsyntax-only")

    return new_parts


def compile_timed(cmd, cwd, timeout=600):
    """Run command, return (elapsed_secs, return_code)."""
    start = time.monotonic()
    r = subprocess.run(cmd, capture_output=True, timeout=timeout, cwd=cwd)
    return time.monotonic() - start, r.returncode


def mean(v): return sum(v) / len(v) if v else 0
def stdev(v):
    if len(v) < 2: return 0
    m = mean(v)
    return math.sqrt(sum((x-m)**2 for x in v) / (len(v)-1))
def fmt(s):
    if s < 1: return f"{s*1000:.0f}ms"
    return f"{s:.2f}s"


def benchmark_config(name, cmd, cwd, warmup, iters):
    """Run warmup + measured iterations, return list of times or None on failure."""
    for _ in range(warmup):
        compile_timed(cmd, cwd)

    times = []
    for _ in range(iters):
        t, rc = compile_timed(cmd, cwd)
        if rc != 0:
            # Show error
            r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, timeout=60)
            print(f"  {name:<20s}: FAILED")
            for line in r.stderr.strip().split('\n')[:3]:
                print(f"    {line[:120]}")
            return None
        times.append(t)

    m, s = mean(times), stdev(times)
    print(f"  {name:<20s}: {fmt(m)} ± {fmt(s)}")
    return times


def main():
    args = parse_args()

    with open(args.compile_commands) as f:
        commands = json.load(f)

    clang_abs = os.path.abspath(args.clang_binary)

    # Filter and sort
    filtered = []
    for entry in commands:
        fp = entry.get("file", "")
        if args.filter and args.filter not in fp: continue
        if not fp.endswith(".cpp") and not fp.endswith(".cc"): continue
        if "test" in os.path.basename(fp).lower(): continue
        full = os.path.join(entry["directory"], fp) if not os.path.isabs(fp) else fp
        entry["_full_path"] = full
        try:
            with open(full) as f:
                content = f.read()
            entry["_lines"] = content.count('\n')
            entry["_arrows"] = content.count('->')
        except:
            entry["_lines"] = 0
            entry["_arrows"] = 0
        filtered.append(entry)

    filtered.sort(key=lambda e: e["_lines"], reverse=True)
    filtered = filtered[:args.num_files]

    print(f"Cross-analysis benchmark on {len(filtered)} real-world files")
    print(f"Warmup: {args.warmup}, Iterations: {args.iterations}")
    print()

    results = []

    for i, entry in enumerate(filtered):
        rel = os.path.relpath(entry["_full_path"])
        lines, arrows = entry["_lines"], entry["_arrows"]
        cwd = entry.get("directory", ".")

        raw = shlex.split(entry["command"]) if "command" in entry else entry["arguments"]
        base = clean_compile_command(raw, clang_abs)

        print(f"[{i+1}/{len(filtered)}] {rel} ({lines} lines, {arrows} '->')")

        r = {"file": rel, "lines": lines, "arrows": arrows, "configs": {}}

        # All configs use -Wno-everything to suppress warning emission,
        # then re-enable ONLY the specific analysis. This isolates pure
        # analysis overhead without diagnostic I/O noise.
        # NOTE: -Wno-everything != -w. -w skips AnalysisBasedWarnings entirely,
        # while -Wno-everything just suppresses emission but still runs analysis
        # for any explicitly re-enabled warning.
        quiet = ["-Wno-everything"]

        # 1. Baseline: -w (skips ALL analysis — the absolute minimum)
        t = benchmark_config("Baseline (-w)", base + ["-w"], cwd, args.warmup, args.iterations)
        r["configs"]["Baseline"] = t

        # 2. Quiet baseline: -Wno-everything (runs analysis infra but emits nothing)
        t = benchmark_config("Quiet baseline", base + quiet, cwd, args.warmup, args.iterations)
        r["configs"]["Quiet"] = t

        # 3. + -Wuninitialized (only this analysis active)
        t = benchmark_config("-Wuninitialized", base + quiet + ["-Wuninitialized"],
                             cwd, args.warmup, args.iterations)
        r["configs"]["Uninit"] = t

        # 4. + -Wthread-safety (only this analysis active)
        t = benchmark_config("-Wthread-safety", base + quiet + ["-Wthread-safety"],
                             cwd, args.warmup, args.iterations)
        r["configs"]["ThreadSafe"] = t

        # 5. + nullsafe (only nullsafe analysis active)
        t = benchmark_config("Nullsafe", base + quiet + [
            "-fflow-sensitive-nullability", "-fnullability-default=nullable"],
                             cwd, args.warmup, args.iterations)
        r["configs"]["Nullsafe"] = t

        if not args.skip_csa:
            # 6. CSA --analyze (separate pass, on top of normal compilation)
            csa_cmd = [p for p in base if p != "-fsyntax-only"] + [
                "--analyze",
                "-Xanalyzer", "-analyzer-output=text",
                "-w",
            ]
            t = benchmark_config("CSA all", csa_cmd, cwd, args.warmup, args.iterations)
            r["configs"]["CSA"] = t

        results.append(r)
        print()

    # Summary
    print("=" * 110)
    print("RESULTS: Cross-Analysis Comparison on Real-World Code (LLVM/Clang)")
    print("=" * 110)
    print()

    config_keys = ["Baseline", "Quiet", "Uninit", "ThreadSafe", "Nullsafe"]
    if not args.skip_csa:
        config_keys.append("CSA")
    config_labels = {
        "Baseline": "Base(-w)",
        "Quiet": "Quiet",
        "Uninit": "-Wuninit",
        "ThreadSafe": "-Wthread",
        "Nullsafe": "Nullsafe",
        "CSA": "CSA(all)",
    }

    # Header
    hdr = f"| {'File':<40s} | {'Lines':>5s} |"
    for k in config_keys:
        hdr += f" {config_labels[k]:>12s} |"
    print(hdr)
    sep = f"|{'-'*42}|{'-'*7}|" + "".join(f"{'-'*14}|" for _ in config_keys)
    print(sep)

    totals = {k: 0 for k in config_keys}
    for r in results:
        row = f"| {r['file'][-40:]:40s} | {r['lines']:>5d} |"
        for k in config_keys:
            t = r["configs"].get(k)
            if t:
                m = mean(t)
                totals[k] += m
                row += f" {fmt(m):>12s} |"
            else:
                row += f" {'FAIL':>12s} |"
        print(row)

    print(sep)
    row = f"| {'TOTAL':40s} | {'':>5s} |"
    for k in config_keys:
        row += f" {fmt(totals[k]):>12s} |"
    print(row)

    # Overhead calculations
    bl = totals["Baseline"]
    if bl > 0:
        print()
        print("### Overhead vs Baseline (-w)")
        print()
        for k in config_keys[1:]:
            if k == "CSA" and totals[k] > 0:
                # CSA total = baseline compile + CSA analyze
                csa_total = bl + totals[k]
                ovh = (csa_total - bl) / bl * 100
                print(f"  CSA total (compile+analyze): {fmt(csa_total)} ({ovh:+.1f}% vs baseline)")
            elif totals[k] > 0:
                ovh = (totals[k] - bl) / bl * 100
                print(f"  {config_labels[k]:<20s}: {ovh:+.1f}%")

    # Overhead vs quiet baseline (apples-to-apples: same warning suppression)
    quiet_total = totals.get("Quiet", 0)
    if quiet_total > 0:
        print()
        print("### Overhead vs Quiet baseline (apples-to-apples)")
        print("    All configs use -Wno-everything. Measures PURE analysis cost.")
        print()
        for k in ["Uninit", "ThreadSafe", "Nullsafe"]:
            if totals[k] > 0:
                overhead = (totals[k] - quiet_total) / quiet_total * 100
                print(f"  {config_labels[k]:<20s}: {overhead:+.1f}%")

    print()
    print("Note: All configs use -Wno-everything to suppress diagnostic emission,")
    print("isolating pure analysis overhead. 'Quiet' = -Wno-everything (analysis")
    print("infra active, no specific analysis). Each analysis config adds ONE flag")
    print("on top of Quiet. Nullsafe uses -fnullability-default=nullable to exercise")
    print("analysis on ALL functions — the maximum possible workload.")
    if not args.skip_csa:
        print("CSA runs as a separate pass (--analyze). Total cost = compile + analyze.")


if __name__ == "__main__":
    main()
