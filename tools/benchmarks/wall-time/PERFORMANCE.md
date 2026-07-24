# Wall-time performance: baseline vs. nullsafe fork vs. Clang Static Analyzer

This documents a wall-time benchmark comparing three ways to run the compiler on
the same C/C++ code:

- **baseline** — `clang -fsyntax-only`, no nullsafe flags (≈ vanilla frontend cost)
- **nullsafe** — `+ -fflow-sensitive-nullability -fnullability-default=nullable`
  (this fork's flow-sensitive null checker)
- **analyzer** — `clang --analyze` (the Clang Static Analyzer, CSA)

For *what each tool catches* (correctness, not speed) see
[`../../../nullsafe-playground/standard-clang-gap.c`](../../../nullsafe-playground/examples/standard-clang-gap.c)
and the architecture note
[`nullsafe-vs-csa.md`](../../../nullsafe-playground/nullsafe-vs-csa.md). This doc is
purely about **cost**.

## Headline

| Comparison | Effect | 95% CI | Significance |
|---|---|---|---|
| nullsafe, worst case (pointer-dense C, `-fsyntax-only`) | **+18.9%** | — | Welch t≈12, p<1e-15 |
| nullsafe, realistic C++ (STL-heavy TU) | +2.4% | overlaps 0 | p≈0.28, **not sig.** |
| **nullsafe, real clang/LLVM TUs (paired, n=24)** | **+4.5% geomean** | +0.25%…+9.03% | p=0.049, just sig. |
| analyzer, realistic C++ | 2.05× slower | — | p≪0.001 |
| **analyzer, real clang/LLVM TUs (paired, n=24)** | **1.95× geomean** | 1.23×…3.09× | p=0.009 |
| analyzer, pointer-dense C worst case | 38.8× slower | — | p≪0.001 |
| analyzer, pathological real TU (`MicrosoftDemangleNodes.cpp`) | **277.8×** (359ms→99.8s) | — | — |

**Takeaway:** the nullsafe fork costs ~5% in practice (and scales with *pointer
density*, not code size, because it's a linear dataflow pass). The static analyzer
costs ~2× typically but is unbounded — it exploded to 278× on one real file. That
unbounded, non-deterministic cost is exactly why CSA can't run on every build and
the fork can.

## Methodology

### Why "same binary, three modes" (and not a separate vanilla clang)

All checked-out build dirs are the *same fork* at different commits — there is no
genuine upstream clang binary in this tree. Building one would **confound** the
measurement (different LLVM build state, options, PGO, etc.). Running the *same
binary* with the analysis on vs. off isolates the **marginal cost of the analysis**
with no compiler-build confounds. This is the more correct design for "how much does
the feature cost", not a limitation.

### Inputs

1. **Synthetic pointer-dense C** — ~2500 generated functions, each full of pointer
   derefs, guards, ternaries, and loops. This *maximizes* the measurable nullability
   work → worst case. (`gen_synth.py`)
2. **Realistic STL-heavy C++** — a small TU using `vector`/`unique_ptr`/`string`/
   `unordered_map`/`algorithm`, preprocessed to a self-contained `.ii` for
   determinism. Parse cost is dominated by templates → typical C++.
3. **Real clang/LLVM translation units** — sampled (seeded RNG) from
   `build/compile_commands.json`, run with their *actual* flags and include paths.
   Sizes span 28 ms → 13 s. This is the real-world number. (`real_tu_bench.py`)

### Timing & statistics

- CPU-pinned with `taskset -c 4` to cut scheduler migration noise.
- Microbenchmarks (inputs 1 & 2): `hyperfine`, 5 warmup + 40–50 timed runs.
- Real TUs (input 3): **paired** design — every TU measured in every mode, so
  per-TU size variance cancels. baseline/nullsafe = best-of-3; analyzer = 1 rep
  (it's expensive, and the effect is huge).
- Significance: pure-Python Welch's / one-sample t-tests with a real
  t-distribution p-value (regularized incomplete beta) — no scipy. (`stats.py`)
- **Correct scale matters.** Real compile times span three orders of magnitude and
  overhead is *multiplicative*, so the primary test is on the per-TU **log-ratio**,
  not raw milliseconds. On raw ms the nullsafe test is p=0.087 (dominated by a few
  9–13 s TUs); on the log-ratio it is p=0.049. Same data, right scale.

## Static Analyzer notes (important caveats on the CSA numbers)

- **Default constraint manager only.** The analyzer runs used `clang --analyze` with
  **no** `-analyzer-constraints` flag → the built-in **RangeConstraintManager**
  (range-based), *not* a SAT/SMT solver.
- **No Z3 / SAT mode.** This build was compiled with `LLVM_ENABLE_Z3_SOLVER=OFF`
  (`clang --analyze -Xclang -analyzer-constraints=z3` errors out). The Z3 crosscheck
  is documented as another 5–10× on top of these numbers — it is **unmeasured and
  unavailable here**. If you rebuild with Z3, expect the analyzer column to get
  dramatically worse.
- **Default checker set.** Numbers depend on which checkers are enabled; alpha
  checkers off by default. CSA cost is *budget-capped* (`max-nodes` ~225k), so on
  huge functions it may bail and silently under-report — slowness is bounded but
  coverage isn't guaranteed.

## Limitations

- Marginal-cost design (same binary), not a from-scratch upstream-vs-fork build.
- Real-TU analyzer arm used 1 rep/TU (noisier; acceptable given the large effect),
  and the run was stopped at n=24 of a planned 30 because the analyzer arm is
  pathologically slow (one file alone took ~100 s).
- Measured on a shared 72-core devserver; CPU-pinning mitigates but can't eliminate
  neighbor noise.
- All results are `-fsyntax-only` (frontend-only). Under a real `-O2 -c` build the
  nullsafe overhead is diluted further by codegen/optimization time.

## Reproduce

```bash
cd tools/benchmarks/wall-time
# 1. worst-case + realistic microbenchmarks (edit CLANG at top if needed)
./run.sh
# 2. real clang/LLVM TUs (paired), then stats
python3 real_tu_bench.py 30 3           # writes real_tu.json + streams a log
python3 analyze.py real_tu.json          # paired stats + significance
```

Raw data from the run described above: `real_tu_run.log`, `B_real.json`.
