# Lifting the proofs from bounded to unbounded

Every result so far is exhaustive over a bounded domain: `matchLength <= 16`,
a 48-byte output buffer. Real matchLengths reach 131074. This is the step that
removes the bound.

## The mechanism works

CBMC has loop contracts, which turn a bounded unwinding into a proof by
induction. `loop-contract-mechanism.c` is the smallest demonstration:

```c
while (i < len)
__CPROVER_assigns(i, __CPROVER_object_upto(buf, len))
__CPROVER_loop_invariant(i <= len)
__CPROVER_decreases(len - i)
{ buf[i] = 7; i++; }
```

```sh
goto-cc --function harness loopinv.c -o li.gb
goto-instrument --apply-loop-contracts li.gb li_inv.gb
cbmc li_inv.gb --bounds-check --pointer-check      # note: no --unwind
```

```
** 0 of 25 failed (1 iterations)
VERIFICATION SUCCESSFUL
```

`len` ranges to 100000 and there is **no unwind bound at all**. One iteration.
That is the difference between "checked up to N" and "proved".

CBMC also ships `goto-synthesizer`, which is meant to infer these invariants. On
a toy it reports `result : PASS` and emits a 178-byte stub, because its internal
check does not use unwinding assertions and so it sees nothing to strengthen.
Invariants are being written by hand instead.

## Applied to ZSTD_wildcopy

`annotate-wildcopy-loop-contract.patch` annotates the real loop:

```c
__CPROVER_assigns(op, ip, __CPROVER_object_whole(dstStart))
__CPROVER_loop_invariant(op <= oend + 31)
__CPROVER_loop_invariant((BYTE*)op - (BYTE*)dst == (const BYTE*)ip - (const BYTE*)src)
__CPROVER_decreases(oend > (BYTE*)op ? (size_t)((BYTE*)oend - (BYTE*)op) : (size_t)0)
```

Two invariants, and they say exactly what the function's own comments claim
informally: the cursors advance in lockstep, and `op` never passes `oend` by
more than one 32-byte pair, which is what `WILDCOPY_OVERLENGTH` of slack exists
to absorb.

`harness_wildcopy.c` allocates both buffers symbolically rather than as fixed
arrays, so `length` is not capped by a buffer size. A discharged proof here is
unbounded, not exhaustive-up-to-N.

### The blocker: CBMC rejects loop contracts on `do`/`while`

The first attempt appeared to instrument cleanly and then unwound forever:

```
Unwinding loop ZSTD_wildcopy.2 iteration 2223
```

`goto-instrument` had in fact refused the contract and said so; the message was
filtered out by a `| tail -1` in the invocation. Reproduced in isolation:

```
Loop contracts are unsupported on do/while loops: file dowhile.c line 10
```

`ZSTD_wildcopy`'s hot loop is a `do`/`while`, so it cannot carry a contract as
written. This is a real toolchain-versus-codebase fit problem and not a
limitation of the method: the standard rewrite works.

```c
do { B } while (C);        ==>      while (1) { B; if (!(C)) break; }
```

`loop-contract-dowhile-rewrite.c` proves the rewrite carries contracts fine:

```
** 0 of 25 failed (1 iterations)
VERIFICATION SUCCESSFUL
```

again with no unwind bound, `len` to 100000. Note the invariant had to change
with the shape: `i <= len` is too weak for the rewritten loop (the write happens
before the exit test), and `i < len` is the right one. Rewriting a loop for
verification changes its invariant, which is a cost worth knowing about in
advance.

`annotate-wildcopy-loop-contract.patch` now carries the rewrite. It is
**proof-only**: behaviour is identical, the body still runs at least once.
`goto-instrument` accepts it and CBMC emits no unwinding at all for that loop,
which is how the patch is confirmed to have taken effect.

**Status: solving, not yet discharged.**

## What is still hand-written, and what that costs

The invariants above are for **memory safety**. Lifting the *functional*
result in `RESULT-lz-correctness.md` to unbounded needs a stronger invariant:
one that states, at every iteration, that the bytes written so far already equal
the bytes the LZ semantics require. That is a harder invariant to write and the
real remaining work on this branch. It is also exactly what a loop `invariant`
clause is for, so the front-end design and the proof effort meet here rather
than running in parallel.
