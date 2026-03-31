#!/usr/bin/env python3
"""
Clang Static Analyzer vs flow-nullability performance comparison.

Measures wall-clock time for:
  1. Clang Static Analyzer (--analyze, core.NullDereference)
  2. Flow-sensitive nullability (-fflow-sensitive-nullability)
  3. Baseline compilation (no analysis)

Both tools find null dereferences, but CSA uses symbolic execution
(path-sensitive, interprocedural) while flow-nullability uses forward
dataflow on the CFG (intraprocedural, like -Wthread-safety).

Usage:
    python3 flow-nullability-csa-benchmark.py \
        --clang-binary /path/to/clang \
        --output-dir csa_benchmark_results
"""

import sys
import argparse
import subprocess
import json
import os
import math
import statistics
import time
from datetime import datetime


# ---------------------------------------------------------------------------
# Code generators — same code for both tools so the comparison is fair
# ---------------------------------------------------------------------------

def generate_null_deref_code(n):
    """
    N functions with null-dereference patterns that both CSA and
    flow-nullability can analyze. Uses _Nullable annotations so
    flow-nullability is exercised, and straightforward pointer patterns
    so CSA's core.NullDereference fires.
    """
    code = """
struct Node {
    int value;
    Node *next;
    Node *left;
    Node *right;
};

Node *getNode();
int getInt();
bool getBool();
"""
    for i in range(n):
        # Mix of safe and unsafe patterns
        code += f"""
// Safe: null-checked before use
int safe_{i}(Node *p) {{
    if (!p) return -1;
    int v = p->value;
    if (p->next) {{
        v += p->next->value;
        if (p->next->left) {{
            v += p->next->left->value;
        }}
    }}
    return v;
}}

// Has a conditional path structure CSA must explore
int branchy_{i}(Node *a, Node *b) {{
    int sum = 0;
    if (a) {{
        sum += a->value;
        if (b) {{
            sum += b->value;
            if (a->next && b->next) {{
                sum += a->next->value + b->next->value;
            }}
        }}
    }}
    return sum;
}}
"""
    return code


def generate_deep_branch_code(n):
    """
    Functions with deep branching that stress CSA's path exploration.
    Each function has independent if-branches, creating 2^k paths
    for k branches.
    """
    code = """
struct Data {
    int x;
    Data *next;
};
Data *getData();
"""
    for i in range(n):
        # Each function has 6 independent nullable pointers = 64 paths
        code += f"""
int deep_{i}(Data *a, Data *b, Data *c,
             Data *d, Data *e, Data *f) {{
    int sum = 0;
    if (a) sum += a->x;
    if (b) sum += b->x;
    if (c) sum += c->x;
    if (d) sum += d->x;
    if (e) sum += e->x;
    if (f) sum += f->x;
    return sum;
}}
"""
    return code


def generate_loop_code(n):
    """
    Functions with loops — CSA unrolls loops (default 4 iterations),
    while flow-nullability uses fixpoint convergence.
    """
    code = """
struct ListNode {
    int val;
    ListNode *next;
};
ListNode *getList();
"""
    for i in range(n):
        code += f"""
int traverse_{i}(ListNode *head) {{
    int sum = 0;
    ListNode *p = head;
    while (p) {{
        sum += p->val;
        p = p->next;
    }}
    return sum;
}}

int traverse_nested_{i}(ListNode *outer) {{
    int sum = 0;
    for (ListNode *p = outer; p; p = p->next) {{
        sum += p->val;
        ListNode *q = p->next;
        if (q) {{
            sum += q->val;
        }}
    }}
    return sum;
}}
"""
    return code


# ---------------------------------------------------------------------------
# Statistics (same as cross-analysis benchmark — no numpy/scipy)
# ---------------------------------------------------------------------------

def t_critical(df, confidence=0.95):
    alpha = 1.0 - confidence
    p = 1.0 - alpha / 2.0
    t_val = math.sqrt(-2.0 * math.log(1.0 - p)) if p < 1.0 else 3.0
    c0, c1, c2 = 2.515517, 0.802853, 0.010328
    d1, d2, d3 = 1.432788, 0.189269, 0.001308
    z = t_val - (c0 + c1 * t_val + c2 * t_val ** 2) / \
        (1.0 + d1 * t_val + d2 * t_val ** 2 + d3 * t_val ** 3)
    g1 = (z ** 3 + z) / (4 * df)
    g2 = (5 * z ** 5 + 16 * z ** 3 + 3 * z) / (96 * df ** 2)
    return z + g1 + g2


def confidence_interval(data, confidence=0.95):
    n = len(data)
    if n < 2:
        return statistics.mean(data), 0.0
    mean = statistics.mean(data)
    se = statistics.stdev(data) / math.sqrt(n)
    t = t_critical(n - 1, confidence)
    return mean, t * se


def _normal_cdf(x):
    a1, a2, a3, a4, a5 = (0.254829592, -0.284496736, 1.421413741,
                           -1.453152027, 1.061405429)
    p = 0.3275911
    sign = 1 if x >= 0 else -1
    x = abs(x)
    t = 1.0 / (1.0 + p * x)
    y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * math.exp(-x * x / 2.0)
    return 0.5 * (1.0 + sign * y)


def paired_t_test(a, b):
    n = len(a)
    diffs = [ai - bi for ai, bi in zip(a, b)]
    mean_d = statistics.mean(diffs)
    if n < 2:
        return 0.0, 1.0, mean_d, 0.0
    sd = statistics.stdev(diffs)
    if sd < 1e-12:
        return float('inf'), 0.0, mean_d, 0.0
    se = sd / math.sqrt(n)
    t_stat = mean_d / se
    p_value = 2.0 * (1.0 - _normal_cdf(abs(t_stat)))
    ci = t_critical(n - 1) * se
    return t_stat, p_value, mean_d, ci


def significance_stars(p):
    if p < 0.001: return "***"
    if p < 0.01:  return "**"
    if p < 0.05:  return "*"
    return "n.s."


def human_time(ms):
    if ms >= 1000: return f"{ms / 1000:.2f}s"
    if ms >= 1:    return f"{ms:.1f}ms"
    return f"{ms * 1000:.0f}µs"


# ---------------------------------------------------------------------------
# Measurement
# ---------------------------------------------------------------------------

def time_command(cmd, timeout=300):
    """Run a command and return wall-clock time in milliseconds."""
    try:
        start = time.perf_counter()
        subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        elapsed = (time.perf_counter() - start) * 1000.0
        return elapsed
    except (subprocess.TimeoutExpired, PermissionError, OSError):
        return None


def measure_config(clang, source_path, output_dir, config_name,
                   flags, warmup, iterations):
    """Warmup + measure wall-clock time for a compilation config."""
    cmd = [clang] + flags + [source_path]

    # Warmup
    for _ in range(warmup):
        time_command(cmd)

    samples = []
    for _ in range(iterations):
        ms = time_command(cmd)
        if ms is not None:
            samples.append(ms)

    return samples


# Configs to benchmark. CSA uses --analyze which is a separate invocation
# (no -o needed). Compilation configs use -c -o /dev/null.
CONFIGS = [
    {
        "name": "baseline",
        "label": "Baseline (compile only)",
        "flags": ["-c", "-o", "/dev/null", "-w", "-std=c++17"],
    },
    {
        "name": "nullsafe",
        "label": "Flow-nullability",
        "flags": [
            "-c", "-o", "/dev/null", "-std=c++17",
            "-fflow-sensitive-nullability", "-fnullability-default=nullable",
        ],
    },
    {
        "name": "csa",
        "label": "Clang Static Analyzer",
        # --analyze runs CSA. We enable only the null-dereference checker
        # for a fair comparison (CSA has dozens of checkers).
        "flags": [
            "--analyze", "-std=c++17",
            "-Xanalyzer", "-analyzer-checker=core.NullDereference",
            "-Xanalyzer", "-analyzer-output=text",
            "-Xanalyzer", "-analyzer-disable-all-checks",
            "-Xanalyzer", "-analyzer-checker=core.NullDereference",
        ],
    },
    {
        "name": "csa_all",
        "label": "CSA (all default checkers)",
        # CSA with all default checkers — this is what users actually run
        "flags": [
            "--analyze", "-std=c++17",
            "-Xanalyzer", "-analyzer-output=text",
        ],
    },
]


def main():
    parser = argparse.ArgumentParser(
        description="CSA vs flow-nullability performance comparison.")
    parser.add_argument("--clang-binary", required=True)
    parser.add_argument("--output-dir", default="csa_benchmark_results")
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=3)
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    # Test that CSA works at all
    test_src = os.path.join(args.output_dir, "test_csa.cpp")
    with open(test_src, "w") as f:
        f.write("void f(int *p) { *p = 1; }\n")
    result = subprocess.run(
        [args.clang_binary, "--analyze", "-std=c++17", test_src],
        capture_output=True, text=True, timeout=30)
    if result.returncode != 0 and "error" in result.stderr.lower():
        print(f"ERROR: CSA doesn't work with this clang binary.")
        print(f"stderr: {result.stderr[:500]}")
        sys.exit(1)
    print("CSA smoke test passed.\n")

    # Source generators and their N values
    workloads = [
        {
            "name": "null_deref",
            "label": "Null-dereference patterns",
            "generator": generate_null_deref_code,
            "n_values": [50, 100, 200, 500, 1000],
        },
        {
            "name": "deep_branch",
            "label": "Deep branching (64 paths/fn)",
            "generator": generate_deep_branch_code,
            "n_values": [50, 100, 200, 500, 1000],
        },
        {
            "name": "loop",
            "label": "Loop traversal",
            "generator": generate_loop_code,
            "n_values": [50, 100, 200, 500, 1000],
        },
    ]

    all_results = {}

    for workload in workloads:
        wl_name = workload["name"]
        all_results[wl_name] = {}

        print(f"\n{'='*70}")
        print(f"  Workload: {workload['label']}")
        print(f"{'='*70}")

        for n in workload["n_values"]:
            print(f"\n  N = {n} functions")
            print(f"  {'-'*40}")

            # Generate source
            code = workload["generator"](n)
            src_path = os.path.join(args.output_dir, f"src_{wl_name}_{n}.cpp")
            with open(src_path, "w") as f:
                f.write(code)

            results = {}
            for config in CONFIGS:
                cname = config["name"]
                print(f"    {config['label']:40s}", end=" ", flush=True)

                samples = measure_config(
                    args.clang_binary, src_path, args.output_dir,
                    cname, config["flags"],
                    args.warmup, args.iterations)

                if not samples:
                    print("TIMEOUT")
                    continue

                mean, ci = confidence_interval(samples)
                print(f"{human_time(mean)} ± {human_time(ci)}")
                results[cname] = {
                    "samples": samples, "mean": mean, "ci": ci
                }

            # Print ratios
            if "baseline" in results:
                base_mean = results["baseline"]["mean"]
                print(f"\n    Overhead vs baseline:")
                for config in CONFIGS[1:]:
                    cname = config["name"]
                    if cname not in results:
                        continue
                    r = results[cname]
                    pair_n = min(len(results["baseline"]["samples"]),
                                len(r["samples"]))
                    _, p_val, _, _ = paired_t_test(
                        r["samples"][:pair_n],
                        results["baseline"]["samples"][:pair_n])
                    pct = ((r["mean"] - base_mean) / base_mean * 100)
                    sig = significance_stars(p_val)
                    print(f"      {config['label']:38s} "
                          f"{pct:+7.1f}%  p={p_val:.4f} {sig}")

            # CSA/nullsafe ratio
            if "csa" in results and "nullsafe" in results:
                ratio = results["csa"]["mean"] / results["nullsafe"]["mean"]
                print(f"\n    CSA / flow-nullability ratio: {ratio:.1f}x")

            if "csa_all" in results and "nullsafe" in results:
                ratio = results["csa_all"]["mean"] / results["nullsafe"]["mean"]
                print(f"    CSA (all checkers) / flow-nullability ratio: {ratio:.1f}x")

            all_results[wl_name][n] = results

    # Generate report
    report = generate_report(all_results, args, workloads)
    report_path = os.path.join(args.output_dir, "csa_comparison_report.md")
    with open(report_path, "w") as f:
        f.write(report)

    # Raw JSON
    raw_path = os.path.join(args.output_dir, "raw_data.json")
    raw_data = {}
    for wl_name, wl_results in all_results.items():
        raw_data[wl_name] = {}
        for n, results in wl_results.items():
            raw_data[wl_name][str(n)] = {
                name: {"samples": r["samples"]}
                for name, r in results.items()
            }
    with open(raw_path, "w") as f:
        json.dump(raw_data, f, indent=2)

    print(f"\n{'='*70}")
    print(f"  Report: {report_path}")
    print(f"  Raw data: {raw_path}")
    print(f"{'='*70}\n")
    print(report)


def generate_report(all_results, args, workloads):
    lines = []
    lines.append("# Clang Static Analyzer vs Flow-Nullability")
    lines.append("")
    lines.append(f"> Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append(f"> Clang: `{args.clang_binary}`")
    lines.append(f"> Method: {args.warmup} warmup + "
                 f"{args.iterations} measured iterations, wall-clock time")
    lines.append(f"> Statistics: 95% CI, paired two-tailed t-test")
    lines.append("")
    lines.append("## What this measures")
    lines.append("")
    lines.append("Both tools catch null pointer dereferences, but they use "
                 "fundamentally different techniques:")
    lines.append("")
    lines.append("| | Clang Static Analyzer | Flow-Nullability |")
    lines.append("|--|:---------------------:|:----------------:|")
    lines.append("| Technique | Symbolic execution (path-sensitive) | "
                 "Forward dataflow on CFG |")
    lines.append("| Scope | Interprocedural | Intraprocedural |")
    lines.append("| Invocation | Separate step (`--analyze`) | "
                 "Part of compilation (`-fflow-sensitive-nullability`) |")
    lines.append("| Complexity | Exponential in paths | "
                 "O(blocks × vars × iterations) |")
    lines.append("")
    lines.append("CSA is more powerful (cross-function, path-sensitive) but "
                 "that power has a cost.")
    lines.append("This benchmark quantifies that cost.")
    lines.append("")

    for workload in workloads:
        wl_name = workload["name"]
        if wl_name not in all_results:
            continue
        wl_results = all_results[wl_name]

        lines.append(f"## {workload['label']}")
        lines.append("")
        lines.append("| N | Baseline | Flow-Nullability | CSA (null only) "
                     "| CSA (all checkers) | CSA/Nullsafe ratio |")
        lines.append("|--:|---------:|-----------------:|"
                     "----------------:|-------------------:|-------------------:|")

        for n in workload["n_values"]:
            if n not in wl_results:
                continue
            results = wl_results[n]

            def fmt(name):
                if name not in results:
                    return "—"
                r = results[name]
                return f"{human_time(r['mean'])} ± {human_time(r['ci'])}"

            # CSA/nullsafe ratio
            if "csa" in results and "nullsafe" in results:
                ratio = f"{results['csa']['mean'] / results['nullsafe']['mean']:.1f}x"
            else:
                ratio = "—"

            lines.append(
                f"| {n} | {fmt('baseline')} | {fmt('nullsafe')} "
                f"| {fmt('csa')} | {fmt('csa_all')} | {ratio} |")

        lines.append("")

    # Summary section
    lines.append("## Interpretation")
    lines.append("")
    lines.append("The Clang Static Analyzer is a powerful tool — it does "
                 "interprocedural symbolic execution,")
    lines.append("explores feasible paths through the program, and catches "
                 "bugs that intraprocedural analysis cannot.")
    lines.append("But that power comes at a cost measured above.")
    lines.append("")
    lines.append("Flow-nullability is designed for a different use case: "
                 "**every build, every file, in-IDE**.")
    lines.append("It trades precision (intraprocedural only) for speed "
                 "(linear in program size).")
    lines.append("CSA is better suited as a periodic deep-analysis tool; "
                 "flow-nullability is better suited")
    lines.append("as a compiler warning that runs on every keystroke.")
    lines.append("")

    return "\n".join(lines)


if __name__ == "__main__":
    main()
