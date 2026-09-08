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
| zlib `inflate_table` | 120 | **11** | 7 + ~33 — **cannot run** |

`ZSTD_wildcopy`'s three include a `do { } while (0)` **macro**, which is not a
loop in the source, and one in a branch the contract's own
`pre (ovtype == ZSTD_no_overlap)` excludes. So the burden is proportional to
what the preprocessor leaves behind, not to the code a maintainer cares about.

`inflate_table` is the honest verdict. Eleven loops means roughly thirty-three
clauses before the seven-clause contract can be checked at all, and several of
those invariants describe canonical Huffman code construction — real
mathematics, not boilerplate. No maintainer will do that to verify one
precondition.

## Why it is damning

A hand-written CBMC harness verifies `inflate_table` **in 32 seconds** and finds
a real defect in it. The contract route cannot run. The thing we built is
strictly worse than the thing it replaces, on the function where it matters
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
