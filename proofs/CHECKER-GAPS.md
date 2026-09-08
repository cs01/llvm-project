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

## 1b. `--apply-loop-contracts` must be its own earlier pass

`goto-instrument --apply-loop-contracts --enforce-contract F` in one invocation
reports **"Loops remain in function F"** even when every loop in `F` carries a
contract. Two passes, loop contracts first, works.

The error is indistinguishable from gap 1 above, so it sent me annotating loops
that were already annotated. Worse, this exact lesson was already recorded for
`e2e` case 4 and I wrote `verify-contract.sh` with both flags in one call
anyway. If a rule is worth writing down it is worth putting in the script rather
than the prose; `verify-contract.sh` now does the two passes so nobody meets
this again.

## 1c. Every loop-shaped construct counts, including macros and dead branches

`ZSTD_wildcopy` reports three loops. One is the `while (1)` anyone would expect;
one is a `do { } while (0)` **macro** that is not a loop in the source; and one
is in a branch the contract's own `pre (ovtype == ZSTD_no_overlap)` excludes.
All three need contracts before any contract on the function can be checked.

The annotation burden is therefore not proportional to the code a maintainer
cares about. It is proportional to every loop-shaped construct the preprocessor
leaves behind.

## 1d. A missing frame surfaces somewhere unrecognisable

Verifying `ZSTD_wildcopy` without its function-level `assigns` fails at

```
[_mm_storeu_si128.assigns.1] line 742 Check that *__P is assignable: FAILURE
```

an SSE intrinsic in a header, with nothing pointing back at the function being
verified or at the clause that is missing. Restoring the `assigns` gives
`0 of 354`. Anyone meeting that message cold would look in the wrong place.

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

## 6. `forall` stops constraining when its range is symbolic, and says nothing

`forall (i : 0, n) lens[i] <= 15` lowers to
`__CPROVER_forall { unsigned long i; (i < n) ==> (lens[i] <= 15) }`. Whether
that assumption does any work depends on whether `n` is concrete, and nothing
warns when it does not.

Measured on zlib's `inflate_table`, same contract, one clause changed:

| the range | result |
|---|---|
| `pre (codes == 5)` — concrete | **3 of 338 failed**, 63 s. The three real properties, identical to spelling the five indices out by hand. |
| `pre (codes >= 1 && codes <= 5)` — symbolic | **29 of 338 failed**, 447 s. |

The 26 extra failures are the tell: `array 'count' upper bound in
count[lens[sym]]`, `offs[lens[sym]]`, `work + sym` out of bounds. Every one of
them is a consequence of `lens[i]` being unconstrained. The quantified
assumption contributed *nothing*, and the run still reported a counterexample —
so it looks like a finding, and it is an artefact.

**Why this is the dangerous shape.** An assumption that is dropped does not fail
loudly; it makes the property set *larger*, and a bigger red number reads like a
better result. The failure mode of a missing `assigns` (gap 1d) is an
unrecognisable error message. The failure mode here is a plausible bug report.

It is also the shape that hides the opposite error. Had those 26 properties been
provable anyway, the run would have gone green with an assumption nobody was
using, and the proof would have meant less than it appeared to.

*Open.* Options, none implemented:

- Refuse a `forall` whose range bound is not a compile-time constant, which is
  honest and narrow but rejects the case a caller most wants.
- Warn, and say what the alternative is.
- Emit an unwound conjunction ourselves when the bound has a known upper limit
  from an earlier clause: `codes <= 5` makes five conjuncts, which is exactly
  what the annotation said before `forall` existed and is what verified.

The third is the useful one, and it is the same trick a reader would do by hand.
Until then, the rule for an author is: **a `forall` bound must be pinned by an
earlier clause to a single value, or the quantifier is decoration.** Run the
control — pin the bound and see whether the failure count moves — before
believing any run that contains one.
