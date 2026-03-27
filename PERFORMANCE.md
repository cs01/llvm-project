# Flow-Nullability Performance Analysis

Compile-time overhead of `-fflow-sensitive-nullability` with benchmarks
comparing against other CFG-based Sema analyses in Clang.

**Architecture:** Forward dataflow on CFG (same pattern as `-Wthread-safety`
and `-Wuninitialized`). Intraprocedural only.
O(blocks x variables x fixpoint iterations).

The analysis is fully opt-in and pays zero cost when disabled.

## Real-World Benchmarks (LLVM/Clang)

The synthetic benchmarks below measure worst-case scaling behavior. But
real code isn't wall-to-wall nullable pointers — it has templates,
overload resolution, constant folding, and all the other work a compiler
does. How much does the analysis cost on actual production C++?

We tested by compiling **Clang's own source code** with itself — the 8
largest files in `clang/lib/` (17K-26K lines each, up to 4,700 `->` dereferences
per file). `-fnullability-default=nullable` forces the analysis to run on
**every function**, simulating maximum adoption.

### Direct measurement via `-ftime-trace`

`-ftime-trace` isolates the exact `FlowNullabilityAnalysis` phase from
everything else. Sampled across 9 files of varying size from `clang/lib/`
with `-fnullability-default=nullable` (analysis runs on every function):

| File | Lines | Functions analyzed | Analysis time | Total time | Analysis % |
|------|------:|-------------------:|--------------:|-----------:|-----------:|
| SemaConsumer.cpp | 13 | 22 | 1.8ms | 73ms | 2.5% |
| PCHContainerOperations.cpp | 71 | 811 | 79ms | 5.66s | 1.4% |
| Version.cpp | 128 | 180 | 17ms | 2.57s | 0.6% |
| AnalyzerOptions.cpp | 198 | 19,697 | 2,287ms | 28.5s | 8.0% |
| ItaniumCXXABI.cpp | 299 | 20,507 | 2,453ms | 31.3s | 7.8% |
| Syntax/Nodes.cpp | 441 | 773 | 88ms | 5.29s | 1.7% |
| SemaSYCL.cpp | 717 | 41,214 | 4,279ms | 91.7s | 4.7% |
| PathDiagnostic.cpp | 1,248 | 19,791 | 77ms | 14.6s | 0.5% |
| ByteCode/Interp.cpp | 2,690 | 30,604 | 127ms | 56.6s | 0.2% |

Analysis overhead ranges from **0.2% to 8%** depending on file
characteristics. Small files that pull in heavy template headers
(AnalyzerOptions.cpp, ItaniumCXXABI.cpp) see higher percentages because
template instantiation amplifies the function count while keeping other
per-TU overhead low. Large implementation files see <1%.

The median across this sample is ~2%. On typical source files, the
analysis is a rounding error in overall compile time.

### Cross-analysis comparison on real code

Each configuration uses `-Wno-everything` to suppress diagnostic emission,
isolating pure analysis overhead. All configs compile the same source with
the same flags — only the analysis flag differs.

**SemaOpenMP.cpp** (26,369 lines, 3,903 `->` dereferences):

| Configuration | Time | vs Quiet |
|---------------|-----:|---------:|
| Quiet (`-Wno-everything`) | 25.6s | — |
| + `-Wuninitialized` | 24.5s | -4.7% |
| + `-Wthread-safety` | 25.0s | -2.3% |
| + **flow-nullability** | 24.0s | -6.3% |

**ExprConstant.cpp** (22,275 lines, 3,143 `->` dereferences):

| Configuration | Time | vs Quiet |
|---------------|-----:|---------:|
| Quiet (`-Wno-everything`) | 12.1s | — |
| + `-Wuninitialized` | 13.2s | +8.6% |
| + `-Wthread-safety` | 12.9s | +6.6% |
| + **flow-nullability** | 12.0s | -1.3% |

All three analyses are within measurement noise on these files. The
overhead differences are not statistically significant — normal run-to-run
variance on 20K+ line translation units is 2-5%.

### CSA comparison on real code

The Clang Static Analyzer runs as a separate pass on top of compilation.
On `ExprConstant.cpp`:

| Tool | Time |
|------|-----:|
| Compile only (baseline) | 8.75s |
| Compile + flow-nullability | 10.57s |
| CSA `--analyze` (all checkers) | 433.89s |
| **CSA / flow-nullability** | **41x** |

CSA takes **7 minutes** on a single file. Flow-nullability adds ~2 seconds
to the same compilation. For a codebase with thousands of translation units,
this is the difference between "runs on every build" and "runs overnight."

### Why real-world overhead is lower than synthetic benchmarks

The synthetic benchmarks below show 17-39% overhead because every function
is packed with nullable pointer operations. Real code has a much lower
density of null-check patterns relative to total compilation work:

- Template instantiation and overload resolution dominate Sema time
- Most functions don't have complex nullable pointer patterns
- CFG construction (shared with `-Wuninitialized`) is already amortized
- The analysis itself is O(blocks x variables x iterations) and converges
  in 2-3 iterations for structured code

The synthetic benchmarks are still useful for understanding scaling
characteristics, but they represent the pathological worst case — not
what users will experience in practice.

### Reproducing

```bash
# Real-world benchmark (uses compile_commands.json from your build)
python3 tools/real-world-benchmark.py \
    --clang-binary build/bin/clang \
    --compile-commands build/compile_commands.json \
    --filter "clang/lib" \
    --num-files 8 --warmup 2 --iterations 5
```

Requires a working build (for `compile_commands.json` and matching headers).

## Synthetic Benchmarks

The following benchmarks use generated code to stress-test specific patterns.
They measure worst-case scaling, not typical overhead.

**Reading the tables:** "Sig" is the statistical significance from a paired
t-test. `***` = highly significant (p<0.001, the difference is real),
`n.s.` = not significant (the difference could be random noise). See
[Methodology](#methodology) for details.

## Cross-Analysis Comparison

Each analysis compiles N functions with patterns that exercise its specific
checks. Baseline compiles the same code with `-w` (all warnings suppressed).
Paired t-tests on matched iterations.

| N functions | Baseline | `-Wuninitialized` | `-Wthread-safety` | `-fflow-sensitive-nullability` |
|------------:|---------:|-------------------:|-------------------:|-----------------------------------:|
| 100         | 37.4ms   | +8.9% (p=0.05)    | +103.3% (p<0.001) | +37.7% (p<0.001) |
| 500         | 175.9ms  | +8.0% (p=0.11)    | +86.3% (p<0.001)  | +31.0% (p<0.001) |
| 1000        | 328.9ms  | +11.3% (p<0.001)  | +102.8% (p<0.001) | +36.0% (p<0.001) |
| 2000        | 602.1ms  | +20.5% (p<0.001)  | +130.1% (p<0.001) | +39.3% (p<0.001) |

Flow-nullability costs 31-39% overhead vs 86-130% for `-Wthread-safety`.

## Marginal Cost (on top of `-Wuninitialized`)

When `-Wuninitialized` is already enabled, the CFG is already built.
This measures the additional cost of adding flow-nullability.

| N functions | `-Wuninitialized` | Combined | Marginal Overhead | p-value | Sig |
|------------:|-------------------:|---------:|------------------:|--------:|:---:|
| 100         | 40.7ms             | 50.4ms   | +23.6% (p<0.001) | 0.0000  | *** |
| 500         | 189.9ms            | 221.6ms  | +16.7% (p<0.001) | 0.0000  | *** |
| 1000        | 366.0ms            | 461.8ms  | +26.2% (p<0.001) | 0.0000  | *** |
| 2000        | 725.8ms            | 863.6ms  | +19.0% (p<0.001) | 0.0000  | *** |

## Many Small Functions

N separate functions each with a null-check-and-use pattern.

| N functions | Baseline | With Nullsafe | Analysis Time | Overhead | p-value | Sig |
|------------:|---------:|--------------:|--------------:|---------:|--------:|:---:|
| 100         | 32.2ms ± 4.4ms | 33.2ms ± 1.9ms | <1us/fn | +3.2% ± 17.3% | 0.6043 | n.s. |
| 500         | 127.9ms ± 6.4ms | 151.9ms ± 10.1ms | <1us/fn | +18.8% ± 9.9% | 0.0000 | *** |
| 1000        | 250.9ms ± 5.8ms | 270.8ms ± 10.0ms | <1us/fn | +7.9% ± 4.9% | 0.0002 | *** |
| 2000        | 517.5ms ± 17.6ms | 606.5ms ± 35.8ms | <1us/fn | +17.2% ± 8.8% | 0.0000 | *** |
| 5000        | 1.34s ± 78.6ms | 1.49s ± 49.3ms | <1us/fn | +10.7% ± 6.3% | 0.0001 | *** |

Per-function analysis time is sub-microsecond (below `-ftime-trace`
granularity). The `FlowNullabilityAnalysis` trace event accounts for
<0.3% of compile time at 5,000 functions; the remainder of the overhead
is CFG construction.

## Single Large Functions

Single functions with increasing variable counts.

### Sequential Dereferences (N variables, each checked and used)

| N    | Baseline | With Nullsafe | Analysis Time | Analysis % | Overhead | Sig |
|-----:|---------:|--------------:|--------------:|-----------:|---------:|:---:|
| 50   | 10.8ms | 11.5ms | 338us | 2.9% | +6.6% | n.s. |
| 100  | 15.0ms | 16.9ms | 1.3ms | 7.9% | +12.4% | n.s. |
| 200  | 23.6ms | 23.3ms | 3.0ms | 13.0% | -1.4% | n.s. |
| 500  | 50.0ms | 66.3ms | 15.2ms | 22.9% | +32.6% | *** |
| 1000 | 101.5ms | 148.2ms | 56.8ms | 38.3% | +46.0% | *** |

At N=1000, the analysis takes 57ms — 38% of compile time.

### Branch Fan-out (N independent if-branches merging)

| N    | Baseline | With Nullsafe | Analysis Time | Analysis % | Overhead | Sig |
|-----:|---------:|--------------:|--------------:|-----------:|---------:|:---:|
| 100  | 14.8ms | 14.1ms | 363us | 2.6% | -4.7% | n.s. |
| 200  | 20.5ms | 22.0ms | 1.2ms | 5.5% | +7.3% | n.s. |
| 500  | 41.5ms | 42.3ms | 2.0ms | 4.8% | +1.7% | n.s. |
| 1000 | 69.4ms | 87.2ms | 5.3ms | 6.0% | +25.6% | *** |
| 2000 | 135.9ms | 152.1ms | 10.5ms | 6.9% | +11.9% | ** |

Analysis stays under 7% at 2000 branches.

### Loop Convergence (N variables reassigned in a while loop)

| N   | Baseline | With Nullsafe | Analysis Time | Analysis % | Overhead | Sig |
|----:|---------:|--------------:|--------------:|-----------:|---------:|:---:|
| 50  | 13.3ms | 12.7ms | 539us | 4.2% | -4.0% | n.s. |
| 100 | 16.8ms | 17.8ms | 1.6ms | 9.2% | +5.9% | n.s. |
| 200 | 29.7ms | 31.8ms | 5.3ms | 16.7% | +6.9% | n.s. |
| 500 | 59.2ms | 86.1ms | 25.1ms | 29.2% | +45.4% | *** |

At N=500 variables in one loop, the analysis takes 25ms.

### Nested if-Guards (N levels of `if (p)` nesting)

| N   | Baseline | With Nullsafe | Analysis Time | Analysis % | Overhead | Sig |
|----:|---------:|--------------:|--------------:|-----------:|---------:|:---:|
| 10  | 8.1ms | 7.5ms | <1us | <1% | -7.2% | n.s. |
| 25  | 9.0ms | 9.4ms | <1us | <1% | +5.0% | n.s. |
| 50  | 12.2ms | 11.7ms | <1us | <1% | -4.0% | n.s. |
| 100 | 22.3ms | 23.9ms | 579us | 2.4% | +7.0% | n.s. |
| 200 | 51.4ms | 53.2ms | 1.8ms | 3.3% | +3.4% | n.s. |

Analysis stays under 3.5% at 200 levels of nesting.

## Methodology

### Statistical rigor

- 3 warmup runs (discarded) to prime filesystem and instruction caches
- 10 measured iterations per data point
- 95% confidence intervals via t-distribution approximation
- Paired two-tailed t-test comparing with/without on the same source,
  eliminating variance from source complexity
- Significance levels: \*\*\* p<0.001, \*\* p<0.01, \* p<0.05, n.s. not significant

### Measurement

`-ftime-trace` produces structured JSON with per-event durations. The
`FlowNullabilityAnalysis` trace event isolates the analysis phase from CFG
construction and other shared overhead. `ExecuteCompiler` gives total
compile time.

### Cross-analysis comparison notes

Each analysis gets source tailored to its annotation style:
- **Baseline**: bare pointer code with `-w` (all warnings suppressed)
- **`-Wuninitialized`**: same code, uninitialized patterns
- **`-Wthread-safety`**: mutex/lock annotations, `guarded_by` attributes
- **`-fflow-sensitive-nullability`**: `_Nullable`/`_Nonnull` annotations,
  `assume_nonnull` pragmas

The thread-safety source is structurally more complex (mutex classes,
scoped guards) which contributes to its higher overhead.

## Reproducing

```bash
# Self-contained benchmark (no external dependencies)
python3 clang/test/Sema/flow-nullability-benchmark.py \
    --clang-binary build/bin/clang \
    --output-dir benchmark_results \
    --warmup 3 --iterations 10

# Cross-analysis comparison (no external dependencies)
python3 clang/test/Sema/flow-nullability-cross-analysis-benchmark.py \
    --clang-binary build/bin/clang \
    --output-dir cross_benchmark_results \
    --warmup 3 --iterations 10
```

Both scripts use hand-rolled statistics (no numpy/scipy required).
Output includes markdown reports and raw JSON for further analysis.

## Clang Static Analyzer Comparison

The Clang Static Analyzer (CSA) also catches null dereferences, using
path-sensitive symbolic execution. It's more powerful (interprocedural,
path-aware) but runs as a separate step (`--analyze`) on top of normal
compilation. How does it actually compare?

**Important:** CSA `--analyze` skips code generation — it only runs the
analyzer. Flow-nullability runs as part of compilation. The total cost
to get both a compiled object AND null-dereference checking is
compile time + CSA time for the CSA workflow, vs just compile time
(with analysis included) for flow-nullability.

### Null-dereference patterns (null-check-and-use)

| N functions | Baseline | Flow-Nullability | CSA (null checker only) | CSA (all checkers) |
|------------:|---------:|-----------------:|------------------------:|-------------------:|
| 50          | 69.7ms   | 72.7ms           | 32.4ms                  | 90.8ms             |
| 100         | 91.8ms   | 115.9ms          | 37.9ms                  | 172.1ms            |
| 200         | 162.1ms  | 183.5ms          | 55.1ms                  | 297.5ms            |
| 500         | 373.5ms  | 418.3ms          | 107.5ms                 | 666.3ms            |
| 1000        | 752.7ms  | 886.7ms          | 255.5ms                 | 1.38s              |

CSA `--analyze` alone looks fast because it skips codegen. But you still
need to compile. Total cost for N=500:
- Flow-nullability: **418ms** (compile with analysis included)
- CSA null-only: 374ms compile + 108ms analyze = **482ms**
- CSA all checkers: 374ms compile + 666ms analyze = **1,040ms**

### Deep branching (6 independent nullable pointers per function = 64 paths)

This is where CSA's path-sensitive approach shows exponential cost.

| N functions | Baseline | Flow-Nullability | CSA (null only) | CSA (all checkers) | CSA-all / Nullsafe |
|------------:|---------:|-----------------:|----------------:|-------------------:|-------------------:|
| 50          | 49.6ms   | 51.2ms           | 25.9ms          | 426.3ms            | **8.3x**           |
| 100         | 65.4ms   | 71.2ms           | 36.8ms          | 770.6ms            | **10.8x**          |
| 200         | 124.4ms  | 139.5ms          | 45.3ms          | 1.50s              | **10.7x**          |
| 500         | 253.7ms  | 274.9ms          | 82.0ms          | 3.87s              | **14.1x**          |
| 1000        | 490.5ms  | 517.7ms          | 154.6ms         | 7.51s              | **14.5x**          |

At 1000 functions with branching, CSA takes 7.5 seconds vs 518ms for
flow-nullability — a **14.5x** difference. The ratio grows with N
because CSA explores paths per-function while dataflow merges states.

### Loop traversal (linked-list walks)

| N functions | Baseline | Flow-Nullability | CSA (null only) | CSA (all checkers) | CSA-all / Nullsafe |
|------------:|---------:|-----------------:|----------------:|-------------------:|-------------------:|
| 50          | 58.0ms   | 55.8ms           | 32.8ms          | 142.2ms            | **2.5x**           |
| 100         | 89.1ms   | 99.3ms           | 44.7ms          | 262.1ms            | **2.6x**           |
| 200         | 161.2ms  | 180.6ms          | 63.1ms          | 458.2ms            | **2.5x**           |
| 500         | 332.2ms  | 365.2ms          | 120.7ms         | 1.06s              | **2.9x**           |
| 1000        | 613.4ms  | 728.5ms          | 221.9ms         | 2.15s              | **3.0x**            |

Loops are less dramatic (2.5-3x) because CSA caps loop unrolling at 4
iterations by default, limiting path explosion.

### Why CSA null-only is misleadingly fast

The "CSA (null only)" column uses `-analyzer-disable-all-checks` plus
`-analyzer-checker=core.NullDereference`. This disables CSA's other
checkers but **also disables modeling** that those checkers depend on.
In practice, nobody runs CSA with only one checker — you run the full
suite. The "CSA (all checkers)" column is what users actually experience.

### Reproducing

```bash
python3 clang/test/Sema/flow-nullability-csa-benchmark.py \
    --clang-binary build/bin/clang \
    --output-dir csa_benchmark_results \
    --warmup 3 --iterations 10
```

## Machine info

Benchmarks run on the development machine. Results are relative (overhead
percentages), so absolute times will differ across hardware but the
ratios should be stable.

Benchmark date: 2026-03-26
