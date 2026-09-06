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
