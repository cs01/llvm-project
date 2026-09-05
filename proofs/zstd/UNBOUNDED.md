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

### The second blocker: `do { } while (0)` macros are loops

After the rewrite the contract still did not attach, and CBMC unwound
`ZSTD_wildcopy.2` past 1900 iterations. `goto-instrument --show-loops` explains
why: `ZSTD_wildcopy` reported **four** loops, two of them at lines inside the
body of the loop being annotated.

```c
#define COPY16(d,s) do { ZSTD_copy16(d,s); d+=16; s+=16; } while (0)
```

CBMC counts the `do { } while (0)` idiom as a loop. Two `COPY16` uses in the body
produce two phantom loops, and the contract attaches to one of those rather than
to the intended one. There is no diagnostic; it simply instruments the wrong
thing.

Inlining the macro in the annotated copy (proof-only, identical semantics) drops
the count to three and the contract attaches:

```
Loop ZSTD_wildcopy.0:  line 229   (the overlap do/while, unannotated)
Loop ZSTD_wildcopy.1:  line 230
Loop ZSTD_wildcopy.2:  line 246   (the annotated while(1))

** 30 of 356 failed (22 iterations)
```

Analysing rather than unwinding forever. This is the practical lesson for
applying loop contracts to real C: **a codebase's macro idioms decide whether
contracts can be attached at all**, and the failure mode is silent
misattachment, not an error.

## Result: ZSTD_wildcopy is memory-safe, unbounded

```
** 0 of 141 failed (1 iterations)
VERIFICATION SUCCESSFUL
```

`length` ranges to 0x40000000 (1 GiB), both buffers are symbolically allocated,
and there is **no `--unwind` at all**. One iteration. This is not "exhaustive up
to N"; it is a proof by induction over the loop, for every length.

The same run also validates the fix in
`FINDING-wildcopy-pointer-subtract.md`. With `diff` computed unconditionally the
result is `1 of 141 failed`, and the one failure is that defect. Moving the
computation inside the overlap branch, where the two pointers are in the same
object and the subtraction is defined, takes it to zero.

### The invariant that worked

```c
__CPROVER_assigns(op, ip, __CPROVER_object_upto(dstStart, length + WILDCOPY_OVERLENGTH))
__CPROVER_loop_invariant(__CPROVER_same_object(op, dstStart))
__CPROVER_loop_invariant(__CPROVER_same_object(ip, (const BYTE*)src))
__CPROVER_loop_invariant(__CPROVER_POINTER_OFFSET(op) >= 16)
__CPROVER_loop_invariant(__CPROVER_POINTER_OFFSET(op) < (__CPROVER_ssize_t)length)
__CPROVER_loop_invariant(__CPROVER_POINTER_OFFSET(ip) == __CPROVER_POINTER_OFFSET(op))
__CPROVER_decreases((__CPROVER_ssize_t)length - __CPROVER_POINTER_OFFSET(op))
```

Two things about it are worth carrying forward:

- **Offsets, not pointer comparisons.** The `assigns` clause havocs `op` and `ip`
  before the invariant is assumed, so a plain `op < oend + 32` asks CBMC to
  compare pointers it does not yet know are valid, and it flags the comparison
  itself. `__CPROVER_same_object` and `__CPROVER_POINTER_OFFSET` are defined on
  any pointer. This took the failure count from 22 to 6.
- **The upper bound is `length`, not `length + 32`.** The exit test runs at the
  bottom of the loop, so at the head `op` has not yet passed `oend`. Getting this
  wrong let `op` sit near the end of the buffer and made the following 32-byte
  copy look out of bounds. This took 6 to 1.

The bound is the interesting one: `WILDCOPY_OVERLENGTH` slack is what makes the
over-copy safe, and the invariant has to say *where* in the buffer the cursor can
be for that to hold, not merely that the slack exists.

## ZSTD_safecopy: annotated, solving

`annotate-safecopy-loop-contract.patch` carries the same treatment for the other
loop-bearing function in the `ZSTD_execSequence` path. Both of its loops are
plain `while`, so the `do`/`while` blocker does not apply, and there are no
`do { } while (0)` macros in either body, so the phantom-loop problem does not
either. Both are confirmed present at the annotated lines:

```
Loop ZSTD_safecopy.0:  line 853   (the length < 8 path)
Loop ZSTD_safecopy.1:  line 891   (the leftovers after wildcopy)
```

The invariants follow the shape that worked for `ZSTD_wildcopy`: capture the
cursor pair in locals immediately before the loop, then state `same_object` plus
offset bounds and a lockstep relation between the two offsets, with `decreases`
measured in offsets rather than pointer difference.

One difference worth noting. The harness includes the whole
`zstd_decompress_block.c` translation unit, because `ZSTD_safecopy` is `static`.
That pulls 386 loops into the goto program even though the harness calls one
function. It is not obviously the bottleneck, but it is a reason a per-function
harness for a `static` function costs more than the same function would if it
were exported.

**Status: solving.** The contracts were accepted without a diagnostic, but
`goto-instrument` reports nothing on success either, so attachment is only
confirmed by the absence of unwinding output in the solve. Not claiming it yet.

## Remaining



## What is still hand-written, and what that costs

The invariants above are for **memory safety**. Lifting the *functional*
result in `RESULT-lz-correctness.md` to unbounded needs a stronger invariant:
one that states, at every iteration, that the bytes written so far already equal
the bytes the LZ semantics require. That is a harder invariant to write and the
real remaining work on this branch. It is also exactly what a loop `invariant`
clause is for, so the front-end design and the proof effort meet here rather
than running in parallel.
