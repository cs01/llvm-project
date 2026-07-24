# Nullsafe Fork vs. Clang Static Analyzer: Two CFG Walkers, Wildly Different Speeds

Both the **nullsafe fork** and the **Clang Static Analyzer (CSA)** start from the same
Clang CFG. Both are, technically, "CFG walkers." But one runs on every keystroke in your
editor and the other is a batch tool you run overnight. This doc explains *why* — the
difference is not the CFG, it's **what state they carry as they walk it**.

---

## TL;DR

| | **Nullsafe fork** | **Clang Static Analyzer** |
|---|---|---|
| Walks the CFG? | Yes | Yes (but really walks a derived graph) |
| State per program point | **One** joined lattice value | **Many** — one per feasible path |
| At `if (p)` it... | **joins** both branches back together | **splits** into two separate path-states |
| Graph it traverses | the CFG itself | the **ExplodedGraph** (CFG × path-states) |
| Cost | ~linear in code size | exponential worst case (budget-capped) |
| Reasoning | nullability **type contracts** + narrowing | **symbolic execution** with a constraint solver |
| Solver | none — set membership | RangeConstraintManager (+ optional Z3) |
| Runs | inside normal `-fsyntax-only` compile, always on | separate `--analyze` pass |
| Output | compiler warning at the source | full **path trace** to a concrete deref |
| False positives | suppressed aggressively (silence > noise) | tolerated, pruned by path feasibility |
| Soundness | unsound by design | sound-ish on explored paths |

**The one-sentence version:** the fork keeps *one fact per variable* and merges at
control-flow joins (fast, approximate); CSA keeps *one whole simulated program state per
path* and never merges (precise, expensive).

---

## The core difference: JOIN vs. SPLIT

This is the entire story. Everything else is a consequence.

Consider:

```c
void f(int *_Nullable p) {
    if (cond)
        p = get_nonnull();   // p is nonnull here
    // <-- merge point
    *p;                      // is p null?
}
```

### Nullsafe fork — JOINS at the merge (dataflow)

The fork keeps **one abstract state per CFG edge**. At the merge point it computes the
**join** of the two incoming edges:

```
edge from then-branch:  { p: narrowed-nonnull }
edge from else-branch:  { p: nullable }
---------------------------------------- join (intersect narrowed, union nullable)
merge entry state:      { p: nullable }   -> warns at *p
```

- One state per program point, no matter how many paths reach it.
- Merges collapse everything back to a single fact → **linear-ish cost**.
- Cannot tell you *which* path made `p` null — it only knows "at this point, nullable."
- This is the same machinery as `-Wuninitialized` and `-Wthread-safety`.

### CSA — SPLITS at the branch (symbolic execution)

CSA never merges. It carries **one full ProgramState per path**:

```
Path A (cond true):   p = $ret_of_get_nonnull, constraint {$ret != 0}   -> *p OK
Path B (cond false):  p = $p,                   constraint {$p ?= 0}    -> *p: split again
                                                                            -> $p==0 : BUG (report path A->B)
                                                                            -> $p!=0 : continue
```

- Number of states explodes with the number of paths.
- Every state is a mini-simulation: it knows the symbolic value of every variable and the
  constraints accumulated along *that specific path*.
- Can produce a concrete trace: "enter with cond=false, assume p is null here, deref here."

> **Mental model.** The fork is a *type-checker with flow narrowing*. CSA is an
> *interpreter that runs your function on symbolic inputs and explores every feasible
> outcome*.

---

## Why "both are CFG walkers" is misleading

- The fork **walks the CFG directly**: reverse-post-order, one pass, revisiting blocks
  until the lattice stops changing (fixpoint). One CFG block = one state slot.
- CSA walks the **ExplodedGraph**, which is the CFG *exploded* by path-state:

  ```
  ExplodedGraph node = <ProgramPoint (where in the CFG), ProgramState (what's known)>
  ```

  A single CFG block can blow up into dozens of ExplodedGraph nodes — one for each
  distinct path-state that reaches it. **That multiplication is the cost, and the power.**

So: same map (CFG), totally different territory actually traversed.

---

## Why the fork is fast af

1. **Bounded state.** One lattice value per CFG edge. The lattice is small: sets of
   narrowed/nullable vars, member paths, bool-guards, aliases. No per-path explosion.
2. **No solver.** "Is `p` non-null here?" is a `DenseSet` membership check
   (`NarrowedVars.contains(p)`), not a constraint query.
3. **Fixpoint, not path enumeration.** It revisits blocks until state stabilizes; merges
   guarantee the work stays proportional to code size. A hard cap
   (`MaxBlockVisits = CFG.size() * 64`) guarantees termination even when aliasing makes the
   lattice non-monotone.
4. **No inlining.** Purely intraprocedural, plus **one** cheap interprocedural summary
   ("this callee always returns nonnull"), computed once per function in call-graph order.
   It never re-simulates callee bodies.
5. **Runs inside the normal compile.** It's a Sema end-of-TU analysis, so it piggybacks on
   the CFG the compiler already built. No separate process, no separate parse.

Net: close to **linear in code size**, cheap enough to run on every build and live in
`clangd` as you type.

## Why CSA is slow

1. **Path explosion.** N branches → up to 2^N path-states. Loops unrolled (default ~4).
2. **Inlining.** Callees inlined up to a stack depth of ~5, multiplying paths further.
3. **Constraint solving.** Every branch calls `assume()`, which consults the
   RangeConstraintManager to check feasibility and prune dead paths. Optional Z3
   cross-check is 5–10× slower again.
4. **Heavy per-node state.** Each ExplodedGraph node carries a full Environment + Store +
   Constraints. Immutable/shared, but still large.
5. **Budget-capped, not complete.** It gives up after `max-nodes` (~225k default), so it
   can silently miss bugs deep in big functions. Slowness is *bounded*, not eliminated.

Net: **exponential worst case**, tamed by budgets. A batch tool, not an on-keystroke check.

---

## What each one buys you

### Fork wins
- Runs everywhere, always, for free (it's part of the compile).
- Predictable, near-linear cost.
- Annotation-driven (`_Nonnull`/`_Nullable`), so it enforces an intended **contract** and
  supports gradual adoption (`-fnullability-default=`, `#pragma clang assume_nonnull`).
- Aggressive false-positive suppression → trustworthy warnings that devs won't `// NOLINT`.

### Fork gives up
- No path traces ("nullable here" but not *how*).
- Shallow interprocedural reasoning (only the nonnull-return summary).
- **Unsound by design**: opaque calls don't invalidate narrowing, `this->` derefs
  suppressed, etc. Real bugs slip through to keep the signal clean.
- Needs annotations to do much.

### CSA wins
- Finds bugs in **un-annotated** code.
- Concrete, actionable path traces.
- Deep interprocedural reasoning via inlining.
- Catches whole classes beyond null (uninit, leaks, use-after-free, taint, …).

### CSA gives up
- Too slow for the edit loop.
- False positives are a real cost (path-feasible ≠ actually reachable at runtime).
- Misses bugs past its node budget / across opaque calls (invalidation).

---

## The precise analogy

| Nullsafe fork | Clang Static Analyzer |
|---|---|
| `-Wuninitialized`, `-Wthread-safety` | `scan-build`, `clang --analyze` |
| Kotlin/Swift null-safety type checking | a bounded model-checker / symbolic executor |
| **Type contract + flow narrowing** | **Symbolic execution + constraint solving** |
| join at merges (dataflow) | split at branches (path-sensitive) |

Same CFG. The fork keeps one fact and merges; CSA keeps one world per path and never
merges. That single decision is the whole difference between "runs as you type" and "run it
in CI overnight."

---

## See it yourself

Dump CSA's ExplodedGraph to watch the `$p == 0` vs `$p != 0` forks:

```bash
clang -cc1 -analyze -analyzer-checker=core.NullDereference \
      -analyzer-dump-egraph=/tmp/eg.dot file.c
dot -Tpng /tmp/eg.dot -o eg.png   # every node is a <ProgramPoint, ProgramState>
```

The fork has no equivalent graph to dump — because there is nothing to explode. It's just
one lattice value flowing along CFG edges to a fixpoint.
