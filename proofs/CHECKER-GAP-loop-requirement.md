# The loop requirement is what makes this unusable, measured

**This is the gap that decides adoption, and it is ours, not CBMC's fault and
not the target's.**

`goto-instrument --enforce-contract F` refuses unless **every loop-shaped
construct in F already carries its own contract**. Not the loops that matter —
all of them.

## Measured

| function | lines | loops needing contracts | clauses to write |
|---|---|---|---|
| zstd `BIT_initDStream` | 45 | 0 | 4 — **verified, 2 s** |
| zstd `ZSTD_wildcopy` | 30 | 3 | 5 + 9 — **verified, 475 s** |
| zlib `inflate_table` | 120 | **11** | 7 + ~33 — **refused** by `--enforce-contract`; **verified in 38 s** from the generated entry point |

`ZSTD_wildcopy`'s three include a `do { } while (0)` **macro**, which is not a
loop in the source, and one in a branch the contract's own
`pre (ovtype == ZSTD_no_overlap)` excludes. So the burden is proportional to
what the preprocessor leaves behind, not to the code a maintainer cares about.

`inflate_table` is the honest verdict. Eleven loops means roughly thirty-three
clauses before the seven-clause contract can be checked at all, and several of
those invariants describe canonical Huffman code construction — real
mathematics, not boilerplate. No maintainer will do that to verify one
precondition.

## Why it was damning

A hand-written CBMC harness verified `inflate_table` **in 32 seconds** and found
a real defect in it. The contract route could not run at all. The thing we built
was strictly worse than the thing it replaced, on the function where it mattered
most.

## The fix, and it is ours to make

`--enforce-contract` needs loop contracts because it checks the `assigns` clause,
and an uncontracted loop can write anywhere. But we do not have to use
`--enforce-contract` to get a harness out of a contract.

**Emit the harness from the `pre` clauses in our own compiler.** A
`-fcontract-emit-harness` mode would turn

```c
void f(char *p, size_t n)
  pre (fresh(p, n))
  pre (n > 0 && n < 64);
```

into

```c
void __contract_harness_f(void) {
  size_t n = nondet_size();
  __CPROVER_assume(n > 0 && n < 64);
  char *p = __CPROVER_allocate(n, 0);
  f(p, n);
}
```

which is precisely the harness a human writes today, generated from the contract
instead of guessed. CBMC then unwinds normally and the loop requirement never
applies. The author still writes only contracts; nobody writes
`__CPROVER_assume`; and the `assigns` checking that needs loop contracts becomes
opt-in for the functions where it is worth the invariants.

That is one emitter mode, and it is the difference between a grammar that
verifies two functions and one that verifies a codebase.

## Resolved

`-fcontract-emit-harness` shipped. `inflate_table` now verifies **from its
contract alone, in 38 seconds**, and reproduces the defect the hand-written
harness found:

```
line 267  pointer outside object bounds in next[(huff >> drop) + fill]   FAILURE  <- a write
line 267  pointer arithmetic, same expression                            FAILURE
line 332  pointer arithmetic: *table + used                              FAILURE
** 3 of 353 failed
```

**The control was run, and it is what makes the red mean something.** Widening
the one clause under test flips it:

| `fresh(*table, N)` | result |
|---|---|
| `(1u << 3) * sizeof(code)` — what the doc-comment promises | 3 of 353 failed, 51 s |
| `16 * sizeof(code)` | **0 of 353 failed**, 1340 s |
| `64 * sizeof(code)` | **0 of 353 failed**, 832 s |

The failing run is 26x cheaper than the clean one, which is the usual asymmetry:
a counterexample needs one path, a proof needs all of them. Note also that the
64-entry proof is *faster* than the 16-entry one — slack removes boundary cases
rather than adding state, so a bigger buffer is not a more expensive proof.

Two things this did **not** fix, stated plainly because the first was
mis-recorded once already:

1. **Loop contracts are not the missing feature — writing them is the cost.**
   `loop_invariant` and `decreases` work, and `ZSTD_wildcopy` is verified
   *through* `--enforce-contract` with its frame checked (5 function clauses +
   9 loop clauses, 475 s). `inflate_table`'s frame still needs eleven triples
   that nobody has written. That is labour, not a capability gap, and calling it
   a wall was wrong.
2. **The quantifier arrived separately.** The first version of this proof pinned
   `codes == 5` and spelled out five indices, because `lens[]` wants "every
   element is at most MAXBITS" and nothing could say it. `forall (i : lo, hi)`
   landed on the branch in 445c3e05cc68, so the clause is now

   ```c
   pre (forall (i : 0, codes) lens[i] <= 15)
   ```

   with `codes` left as a range rather than a constant. **The timings recorded
   above are from the pinned version**, which is what was actually run; the
   quantified contract is being re-measured and this section will carry its
   numbers or say why it is slower.
