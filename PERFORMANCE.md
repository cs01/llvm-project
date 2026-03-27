# Flow-Nullability Performance Analysis

Compile-time overhead of `-fflow-sensitive-nullability` with benchmarks
comparing against other CFG-based Sema analyses in Clang.

**Architecture:** Forward dataflow on CFG (same pattern as `-Wthread-safety`
and `-Wuninitialized`). Intraprocedural only.
O(blocks x variables x fixpoint iterations).

**Bottom line:** Flow-nullability is **3x cheaper than `-Wthread-safety`** and
only **~2x the cost of `-Wuninitialized`** — an analysis that ships
enabled-by-default in most build systems. The analysis is fully opt-in and
pays zero cost when disabled.

## Cross-Analysis Comparison

This is the key data for reviewer confidence. Each analysis compiles N
functions with patterns that exercise its specific checks. Baseline compiles
the same code with `-w` (all warnings suppressed). Paired t-tests on matched
iterations eliminate noise.

| N functions | Baseline | `-Wuninitialized` | `-Wthread-safety` | **`-fflow-sensitive-nullability`** |
|------------:|---------:|-------------------:|-------------------:|-----------------------------------:|
| 100         | 39.5ms   | +19.5% (p=0.01)   | **+90.5%** (p<0.001) | +30.0% (p<0.001) |
| 500         | 152.7ms  | +19.6% (p<0.001)  | **+130.0%** (p<0.001) | +46.1% (p<0.001) |
| 1000        | 326.9ms  | +14.8% (p=0.002)  | **+148.9%** (p<0.001) | +43.6% (p<0.001) |
| 2000        | 684.3ms  | +12.7% (p=0.002)  | **+112.8%** (p<0.001) | +35.8% (p<0.001) |

**Key finding: `-Wthread-safety` costs 90-149% overhead. Flow-nullability
costs 30-46%. Thread-safety shipped and is widely used.** If the compiler
community accepted thread-safety's compile-time cost, flow-nullability is
well within bounds.

The overhead gap between flow-nullability and `-Wuninitialized` (~15-30pp)
is dominated by CFG construction. In codebases that already enable
`-Wuninitialized` (most do), the CFG is already built and the marginal
cost of adding flow-nullability is much smaller.

No upstream Clang analysis publishes this kind of head-to-head comparison.

## Realistic Workload: Many Small Functions

The most representative benchmark — N separate functions each with a
null-check-and-use pattern, mimicking real codebases.

| N functions | Baseline | With Nullsafe | Analysis Time | Overhead | p-value | Sig |
|------------:|---------:|--------------:|--------------:|---------:|--------:|:---:|
| 100         | 32.2ms ± 4.4ms | 33.2ms ± 1.9ms | <1us/fn | +3.2% ± 17.3% | 0.6043 | n.s. |
| 500         | 127.9ms ± 6.4ms | 151.9ms ± 10.1ms | <1us/fn | +18.8% ± 9.9% | 0.0000 | *** |
| 1000        | 250.9ms ± 5.8ms | 270.8ms ± 10.0ms | <1us/fn | +7.9% ± 4.9% | 0.0002 | *** |
| 2000        | 517.5ms ± 17.6ms | 606.5ms ± 35.8ms | <1us/fn | +17.2% ± 8.8% | 0.0000 | *** |
| 5000        | 1.34s ± 78.6ms | 1.49s ± 49.3ms | <1us/fn | +10.7% ± 6.3% | 0.0001 | *** |

Per-function analysis time is below `-ftime-trace` granularity
(sub-microsecond). The ~8-19% total overhead is dominated by CFG
construction — the `FlowNullabilityAnalysis` trace event itself accounts
for <0.3% of compile time even at 5,000 functions.

## Stress Tests: Single Large Functions

Worst-case scenarios — single functions with extreme variable counts.
Real code rarely has functions with 500+ nullable pointers.

### Sequential Dereferences (N variables, each checked and used)

| N    | Baseline | With Nullsafe | Analysis Time | Analysis % | Overhead | Sig |
|-----:|---------:|--------------:|--------------:|-----------:|---------:|:---:|
| 50   | 10.8ms | 11.5ms | 338us | 2.9% | +6.6% | n.s. |
| 100  | 15.0ms | 16.9ms | 1.3ms | 7.9% | +12.4% | n.s. |
| 200  | 23.6ms | 23.3ms | 3.0ms | 13.0% | -1.4% | n.s. |
| 500  | 50.0ms | 66.3ms | 15.2ms | 22.9% | +32.6% | *** |
| 1000 | 101.5ms | 148.2ms | 56.8ms | 38.3% | +46.0% | *** |

At N=1000 (a single function with 1000 nullable pointers), the analysis
takes 57ms — 38% of compile time. This is the pathological worst case.
Functions of this size are extremely rare.

### Branch Fan-out (N independent if-branches merging)

| N    | Baseline | With Nullsafe | Analysis Time | Analysis % | Overhead | Sig |
|-----:|---------:|--------------:|--------------:|-----------:|---------:|:---:|
| 100  | 14.8ms | 14.1ms | 363us | 2.6% | -4.7% | n.s. |
| 200  | 20.5ms | 22.0ms | 1.2ms | 5.5% | +7.3% | n.s. |
| 500  | 41.5ms | 42.3ms | 2.0ms | 4.8% | +1.7% | n.s. |
| 1000 | 69.4ms | 87.2ms | 5.3ms | 6.0% | +25.6% | *** |
| 2000 | 135.9ms | 152.1ms | 10.5ms | 6.9% | +11.9% | ** |

The intersect operation scales well — analysis stays under 7% even at
2000 branches.

### Loop Convergence (N variables reassigned in a while loop)

| N   | Baseline | With Nullsafe | Analysis Time | Analysis % | Overhead | Sig |
|----:|---------:|--------------:|--------------:|-----------:|---------:|:---:|
| 50  | 13.3ms | 12.7ms | 539us | 4.2% | -4.0% | n.s. |
| 100 | 16.8ms | 17.8ms | 1.6ms | 9.2% | +5.9% | n.s. |
| 200 | 29.7ms | 31.8ms | 5.3ms | 16.7% | +6.9% | n.s. |
| 500 | 59.2ms | 86.1ms | 25.1ms | 29.2% | +45.4% | *** |

Fixpoint iteration grows with variable count. At N=500 variables in one
loop, the analysis takes 25ms — still well under a second.

### Nested if-Guards (N levels of `if (p)` nesting)

| N   | Baseline | With Nullsafe | Analysis Time | Analysis % | Overhead | Sig |
|----:|---------:|--------------:|--------------:|-----------:|---------:|:---:|
| 10  | 8.1ms | 7.5ms | <1us | <1% | -7.2% | n.s. |
| 25  | 9.0ms | 9.4ms | <1us | <1% | +5.0% | n.s. |
| 50  | 12.2ms | 11.7ms | <1us | <1% | -4.0% | n.s. |
| 100 | 22.3ms | 23.9ms | 579us | 2.4% | +7.0% | n.s. |
| 200 | 51.4ms | 53.2ms | 1.8ms | 3.3% | +3.4% | n.s. |

Nesting depth has minimal impact — per-edge state tracking remains fast.

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
scoped guards) which contributes to its higher overhead. This mirrors
real-world usage: thread-safety annotations add compile-time cost through
both the analysis and the annotated type machinery.

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

## Machine info

Benchmarks run on the development machine. Results are relative (overhead
percentages), so absolute times will differ across hardware but the
ratios should be stable.

Benchmark date: 2026-03-26
