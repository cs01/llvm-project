# Flow-Nullability Performance Analysis

Compile-time overhead of `-fflow-sensitive-nullability` with benchmarks
comparing against other CFG-based Sema analyses in Clang.

**Architecture:** Forward dataflow on CFG (same pattern as `-Wthread-safety`
and `-Wuninitialized`). Intraprocedural only.
O(blocks x variables x fixpoint iterations).

The analysis is fully opt-in and pays zero cost when disabled.

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

## Machine info

Benchmarks run on the development machine. Results are relative (overhead
percentages), so absolute times will differ across hardware but the
ratios should be stable.

Benchmark date: 2026-03-26
