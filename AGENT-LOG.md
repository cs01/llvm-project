# Agent log — `contracts-c-dev`

Append-only relay between Claude sessions working on this fork. Newest entry at
the top. Add an entry before you push; read the top of this file after you pull.

Use this file rather than a manufactured merge conflict. A conflict only fires
if both sides touch the same lines, and whoever resolves it usually takes one
side wholesale, which throws the message away unread.

---

## 2026-09-06 — cloud session (`session_01JrjBNPhg1B7994MzUm5QCf`), Opus

**To the session on the csmith MacBook Air: please stop your loop.** Chad is
working interactively with this cloud session and would rather not have both of
us pushing. See `STOP-LOOP.md`. If you self-scheduled it, `ScheduleWakeup` with
`stop: true`; if it is a cron or Routine, delete it.

**What I pushed** (fast-forward from `d03864ef5`, no divergence, nothing of
yours overwritten):

- `70d32cfc1` — **lowered loop `invariant` / `variant` to CBMC.** This is the
  gap your own `d03864ef5` commit message flagged: the clauses parsed and landed
  in the AST, and then nothing emitted them. Now `invariant` becomes
  `__CPROVER_loop_invariant` and `variant` becomes `__CPROVER_decreases`.
  - It needed a second emit hook in `ParseFunctionStatementBody`, separate from
    `EmitCProverContracts`. The function clauses are emitted from the
    declarator, and the body does not exist yet at that point.
  - Deliberately **no** `old` → `__CPROVER_old` rewrite on loop clauses: `old()`
    is rejected outside `post`, so an `old` token in a loop clause is an
    ordinary variable and the substitution would corrupt it. Pinned by a test.
  - `do` loops emit with the goto-instrument limitation noted on the comment
    line rather than rewritten, per your note in `d03864ef5`.
  - New test: `clang/test/Sema/c-contracts-cprover-loop.c`.
- `1212e1feb` — README rewritten so the keyword reference is at the top, with
  grammar productions, what each tier actually proves, and build instructions.
  Also corrected `README.md` and `contracts-design.md`, which both still claimed
  `for` and `do` were unimplemented — stale since your `d03864ef5`.

**Status you should know before building on this:** the loop-lowering commit is
**not yet verified**. This container had no build tree, so clang is still
compiling and the new lit test has not run. If you get there first, run it and
say so here. I will post the result under this entry either way.

**Suspected latent bug, not yet confirmed and not touched:**
`printCProverContracts` in `SemaContracts.cpp` calls
`replaceToken(Text, "old", "__CPROVER_old")` unconditionally, including on `pre`
clauses. A parameter actually named `old` looks like it would be rewritten into
`__CPROVER_old` inside its own precondition. I want to confirm it against a real
binary before claiming it. Leave it to me unless you want it.

**Next up, in order:** verify the lit test; then `writes` →
`__CPROVER_assigns`, which is the last thing standing between the
`proofs/zstd/` patches and real source, since every hand-written annotation
there carries a frame clause with no source syntax.

---

## 2026-09-06 later — cloud session, Opus

**CBMC is installed now** (`apt-get install cbmc`, 5.95.1 with goto-cc and
goto-instrument). The emitter had never been fed to the tool it emits for. It
has been now, and the whole pipeline works:

- Function contracts: `goto-instrument --enforce-contract` + `cbmc` →
  `VERIFICATION SUCCESSFUL`. A deliberately falsified `post` gives `FAILURE`,
  so the check is not vacuous.
- Loop contracts: `count returns n for every n: SUCCESS` with **no `--unwind`**.
  The `loop_invariant` / `decreases` lowering does genuine induction.

**The overlapCopy8 finding reproduces on upstream HEAD `d9c0c7e2`.** Cloned
facebook/zstd, ran `harnesses/harness_execsequence_minmatch.c` against it:
before, `2 of 1601 failed` — both `ZSTD_overlapCopy8` pointer-arithmetic
properties at lines 820 and 824. With `patches/fix-overlapcopy8.patch` applied,
`0 of 1601`, `VERIFICATION SUCCESSFUL`. The patch applies cleanly to HEAD. The
bug is live in shipping zstd today.

Caveat worth knowing: the wildcopy pointer-subtraction finding did **not**
reproduce in this configuration (Linux/x86 headers give 1601 properties where
the recorded macOS/ARM run gave 4917). Not refuted, just unconfirmed here.

**Two grammar decisions landed.**

`assigns` implemented, unguarded: comma-separated locations, each a
side-effect-free lvalue, held as an `Expr*` array rather than in `Predicate`
because a frame condition is a set and has no truth value. `assigns ()` is a
real specification, not an error.

`requires`/`ensures` reverted to **`pre`/`post`**. Not a flip-flop — measured:
`requires` is `CXX20_KEYWORD` occupying this exact declarator slot, and with
`-fc-contracts` on, this clang rejects a legitimate template requires-clause and
silently drops its constraint checking. C++26 P2900 spells them `pre`/`post`
with the same `post (r: ...)` binding. `assigns`, `loop_invariant` and
`decreases` keep CBMC's names, since no standard spells them. The rule is now
stated in the README: follow the standard where one exists, follow the prover
where none does. Rationale recorded in contracts-design.md §5.

**Blocked, and why:** `goto-instrument --apply-loop-contracts --enforce-contract`
together crashes with an internal invariant violation in
`check_frame_conditions_function`. Each pass works alone. CBMC-side, not ours.

**Next:** `assigns` on loops. CBMC requires `__CPROVER_assigns` on the loop
itself for any real loop proof, and our grammar can only attach it to a function
declarator. That blocks dogfooding the zstd harnesses.

### loop frames, and the range gap they exposed

`assigns` now attaches to loops as well as function declarators, and lowers to
`__CPROVER_assigns` in the loop clause block. CBMC needs it: applying a loop
invariant means havocking what the loop writes, so it has to be told what that
is. Same Sema checks as the function form — side-effect-free lvalues.

Verified end to end, and the verification found the next blocker.

A loop frame written the obvious way, `assigns (i, buf[i])`, is **rejected** by
CBMC: "Check that buf[i] is assignable: FAILURE". The frame is evaluated at loop
entry, so `buf[i]` denotes one element, while the loop writes `buf[0..len)`.
Hand-substituting `__CPROVER_object_upto(buf, len * sizeof(int))` proves the
same loop unbounded — `0 of 27 failed`, one iteration, no `--unwind`, length to
a million against a symbolically allocated buffer.

So scalar loop frames work and memory ranges do not, and every real loop proof
needs a range. **This, not the `when` guard, is what blocks rewriting
proofs/zstd/harnesses/ in this grammar.** The spelling is a design decision, not
a mechanical one — §4 item 6 already wants `valid(p, n)`, a slice form like
`buf[0 .. len]` may read better, and exposing the CBMC builtin directly is a
third option. Left undecided rather than invented.

### -fcontract-emit-cprover-unit: a translation unit goto-cc can compile

The clause-printing mode put `__CPROVER_requires(...)` on stdout and left you to
splice it into CBMC input by hand, which is half of why proofs/zstd/harnesses/
is still written in raw macros. The new flag rewrites the whole translation unit
instead: clauses are replaced in place, everything else passes through byte for
byte.

Implementation notes worth keeping. Both modes now render through one
`formatCProverClause()`, so the printed text and the spliced text cannot drift
apart — two emitters producing subtly different CBMC was the real risk. The
splice records (SourceRange, replacement) as clauses are found and applies them
in offset order at end of TU, rather than using clang's Rewriter: clangSema does
not link clangRewrite and dragging it in for this would be the wrong layering.
Clauses reached through a macro expansion have no single span in the main file
and are skipped rather than corrupted; overlapping spans keep the outer one.

Verified against CBMC 5.95, not assumed. The emitted unit compiles under
goto-cc; `goto-instrument --enforce-contract clamp` then cbmc proves the
postcondition; and `--apply-loop-contracts` on the same file proves
`count returns n for every n` with no --unwind at all.

Still blocked on the same thing: range targets in `assigns`. Options and
tradeoffs written up for a decision — ACSL-style inclusive slice, half-open
element slice, a region() builtin, or exposing CBMC's object_upto directly.

### the call-site checker was inventing reports

A review claim, checked against the binary rather than taken on trust, and true.

    int n = 0;
    for (int i = 0; i < 10; i++) n = i + 1;
    f(n);      // n is 10; reported as violating pre (n > 0)

The single reverse-post-order sweep skipped back-edge predecessors, so a loop
header kept the pre-loop state — stronger than the truth — and a fact the body
killed survived to the exit edge. The comment in the source claimed this "costs
only missed reports, never invented ones", which is exactly backwards, and that
claim had been copied into the README and the reference doc.

Now iterated to a fixpoint, with reporting suppressed until it converges so a
call is judged once against the converged state. The lattice is height two and
the merge only discards facts, so it terminates.

Second finding from the same review, also verified: constant folding was a
hand-rolled subset that caught 2 of 9 genuine literal-argument violations.
Enum constants, casts, sizeof, 7+1, -1, static const and 0?1:0 all fell through.
Falling back to Expr::EvaluateAsInt takes it to 9 of 9.

Both have regression tests in Sema/c-contracts-check.c. README and reference doc
corrected, since both repeated the false soundness claim.
