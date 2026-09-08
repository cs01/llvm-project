# What dogfooding the checker on real code has exposed

Gaps in **our own tooling**, found by pointing `verify-contract.sh` at real
functions in zstd, zlib and expat. Findings about the *target* code live in
[FINDINGS.md](FINDINGS.md); this file is about the thing we built.

## 1. A function contract needs a loop contract on every loop in the function

`goto-instrument --enforce-contract` refuses while any loop is unconstrained:

```
Reason: Loops remain in function 'inflate_table', assigns clause checking
        instrumentation cannot be applied.
```

It is a real requirement — CBMC cannot check a frame while a loop can write
anywhere — but it was a dead end: the message names neither the loops nor the
remedy. `verify-contract.sh` now lists them and says what to add.

**The cost is the finding.** `inflate_table` is one function and it has **ten**
loops, so verifying its documented contract means writing ten
`assigns`/`loop_invariant`/`decreases` triples first. For a maintainer deciding
whether to annotate a codebase, that ratio matters more than any feature in the
grammar, and nothing in our documentation mentioned it.

*Open:* whether the frame check can be skipped for loops that provably do not
write outside it, so a `pre`-only contract does not drag in the whole body.

## 2. `static inline` functions vanish before instrumentation

`goto-cc` drops a `MEM_STATIC`/`static inline` function nobody calls, and
`--enforce-contract` then reports only `Function 'X' was not found in the GOTO
program`, which reads like the annotation failed. `verify-contract.sh` emits a
reference to force the symbol; that constrains nothing, since every input
constraint still comes from the contract, but it has to be done and the error
does not hint at it.

Most interesting functions in a header-heavy C library are exactly this shape,
so this was hit on the first real target.

## 3. A counterexample was being thrown away by our own solver dispatcher

`solve.sh` rejected any run containing an `UNKNOWN` property, on the reasoning
that an undecided run has not answered. That is right for a *proof* and wrong
for a *counterexample*: CBMC exited 10 with a concrete trace and the race
discarded it, reporting "no solver finished" for 900 seconds.

Fixed by splitting the two cases — a failure is definitive however much else is
undecided; a success requires everything decided. The general form of the
mistake is worth keeping in mind: **a filter meant to prevent false confidence
suppressed a true finding**, which is the same failure as over-tightening a
detector until it silences the bug it was written for.

## 4. `rc=6` was diagnosed as a hard problem

When every solver exits 6 the goto program is malformed and no solver will ever
finish, but the dispatcher printed "symex-bound, not solver-bound: shrink the
harness" — advice that would have sent someone to rewrite a perfectly good
harness. It now distinguishes the two.

## 5. `post` cannot name a by-value parameter, and real contracts want to

Relaxed once already for dereferences, because `ZSTD_overlapCopy8`'s own
documented postcondition is `*op - *ip >= 8` and was unwritable. The remaining
restriction is deliberate — CBMC reads a bare parameter in `ensures` as its
entry value, so `old()` is for the reader — but it is friction every author
meets, and it was met three times in one session.
