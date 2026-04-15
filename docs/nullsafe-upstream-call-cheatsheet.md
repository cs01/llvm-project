# Nullsafe Upstream Call Cheat Sheet

Use this as a speaking aid for a conference call about the upstream PR.

## 10-Second Pitch

"This PR adds an opt-in flow-sensitive nullability warning pass to Clang for C and C++. It lets the compiler use local control flow to catch unchecked uses of nullable pointers during normal compilation."

## 30-Second Pitch

"The upstream branch is a focused compiler change. It adds new flags, new warning groups, and a new intraprocedural dataflow analysis that tracks when a nullable pointer has been checked and can be treated as non-null. That means Clang can now warn on unchecked nullable dereferences and related contract violations in ordinary builds, without requiring a separate analyzer run."

## What This PR Is

- A Clang warning pass.
- Opt-in.
- Flow-sensitive.
- Intraprocedural.
- Designed for normal compile-time use.
- Gradual-adoption friendly.

## What This PR Is Not

- Not a whole-program analysis.
- Not a SAT solver.
- Not a memory-safety system.
- Not nullability inference.
- Not equivalent to all of Crubit.
- Not the playground/docs/release automation from the dev branch.

## What It Catches

- Dereference of nullable pointers: `*p`, `p->x`, `p[i]`.
- Pointer arithmetic on nullable pointers.
- Returning nullable from a `_Nonnull` function.
- Assigning nullable into a `_Nonnull` variable.
- Passing nullable to a `_Nonnull` parameter.

## What Makes It Better Than Stock Clang

- Stock Clang mostly has type-based nullability checks.
- This adds flow-sensitive checking.
- It respects null checks like `if (!p) return;`.
- It can warn on obvious unchecked `_Nullable` dereferences that stock Clang misses.

## What Makes It Different From The Static Analyzer

- Runs as part of normal compilation.
- Meant to be cheap enough for routine use.
- Narrower than symbolic execution.
- Better fit for warning-driven adoption.

## What Makes It Different From Crubit

Say this:

"This is best thought of as upstreaming a focused slice of Crubit-style nullability verification into Clang proper, not upstreaming Crubit as a whole."

Then add:

- Crubit has a broader nullability stack.
- Crubit includes inference as well as verification.
- Crubit has Rust/C++ interop motivations.
- This PR is narrower and more upstream-friendly.
- In some cases Crubit can prove facts this pass intentionally does not.

## Main Limitation To Be Ready To Admit

If someone asks for the biggest gap, say:

"It is intentionally intraprocedural and non-disjunctive. If the proof requires whole-program reasoning or SAT-style reasoning across alternatives, this pass usually won’t try to prove it."

Canonical example:

```c
if (!p1 && !p2) return;
if (p1)
  *p1;
else
  *p2;   // this pass still warns here
```

Talking point:

"Crubit can prove that pattern safe; this Clang pass intentionally does not."

## Why The Upstream Branch Is Smaller Than The Fork

Say this:

"The dev branch is the productized fork. The upstream branch is the reviewable compiler core."

If needed:

- Upstream branch: core analysis, flags, diagnostics, tests.
- Dev branch: docs, benchmarks, playground, install/release automation, extra polish.

## Adoption Story

Say this:

"The rollout story is gradual. The checker only runs where code has opted in through annotations, nullability defaults, or `assume_nonnull` regions."

Useful follow-up:

- `-fflow-sensitive-nullability` enables the analysis.
- `-fnullability-default=nullable` is the aggressive migration mode.
- `-fnullability-default=nonnull` is the ergonomic mode.
- Existing annotated code benefits immediately.

## If Someone Asks “Why Upstream This?”

- It closes a real gap in Clang’s existing nullability story.
- It brings nullability checking into the normal compiler workflow.
- It is easier to deploy than a separate tool.
- It supports incremental adoption in large codebases.
- It gives users a practical middle ground between no checking and heavyweight analysis.

## If Someone Asks “Why Not Just Use Crubit?”

- Crubit is broader than this problem.
- Crubit is not the same thing as an upstream Clang warning pass.
- This PR is a smaller, cleaner LLVM contribution.
- Compiler-native warnings have a lower adoption barrier.

## Safe Claims

- "This improves Clang’s nullability checking with flow-sensitive reasoning."
- "This is an opt-in warning pass."
- "This is designed for gradual adoption."
- "This ports a meaningful subset of nullability-checking behavior into Clang proper."
- "This is narrower than Crubit and intentionally so."

## Claims To Avoid

- "This solves null safety in C/C++."
- "This is equivalent to Crubit."
- "This proves pointers are safe in general."
- "This does whole-program reasoning."
- "This infers nullability automatically."
- "This is a full memory-safety solution."

## Good Closing Line

"The key idea is simple: if users already express nullability in Clang, the compiler should be able to use ordinary control flow to catch obvious unchecked nullable-pointer bugs. That is what this upstream patch adds."
