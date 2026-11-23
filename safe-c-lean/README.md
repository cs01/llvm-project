# safe-c-lean

A small Lean 4 model of the rules behind `-fsafe-c`, built to find holes in
the rules themselves, not in clang. It has a soundness theorem for the rules
with the proposed fixes, and kernel-checked counterexample programs for the
rules as they stand (from `safe-c-notes/review-2026-11-23.md`).

```
lake build      # Lean 4.36.0, core only (no Mathlib)
```

The build is clean: no `sorry`, no warnings. `#print axioms SafeC.safe_c_sound`
lists only `propext`, `Classical.choice` and `Quot.sound`.

## Files

- `SafeC/Defs.lean`: syntax, semantics, typing rules, and the `Rules` switches.
- `SafeC/Sound.lean`: the invariant, the lemmas, and the main theorem.
- `SafeC/Holes.lean`: one well-typed program per review hole, run in the
  kernel (`by decide`) to `ub` under the rule that has the hole and to `trap`
  under the fix. Also shows that no conjunct of `Rules.Sound` can be dropped.
- `SafeC/Own.lean`, `SafeC/OwnSound.lean`, `SafeC/OwnHoles.lean`: a separate
  model of the M2 pointer kinds (owned, borrow, exclusive, unchecked), `free`,
  `realloc`, stack scopes, globals and views, with its own soundness theorem
  (`own_sound`: no UB, so no use after free, double free or dangling stack
  access) and one counterexample per ownership rule. See `OWNERSHIP.md`.

## What is modeled

- **Values**: integers, pointers `(block, offset, lo, hi)`, `null`, `uninit`.
  Outcomes are `ok`, `trap`, `ub`, and (for statements) `outOfFuel`.
- **Memory**: a list of blocks. Each block has a length and a ghost element
  type that only the invariant reads. Nothing is ever freed.
- **Pointer types**: `ptr q k t` with `q` nonnull or nullable, and `k` one of
  single, `counted n`, indexable (bounds carried in the value), or
  null-terminated. `Ty.iface` is the default for an unannotated interface
  pointer (nonnull single).
- **Expressions**: literals, typed `null`, variables, `+ - * / <`, `p[i]`
  (`*p` is `p[0]`), `p + i`, kind conversion (including the checked
  conversion to null-terminated, which scans `[off, hi)` for a 0 and traps if
  there is none), and pointee casts (also standing for union punning).
- **Statements**: assignment, `p[i] = e`, `calloc` (`alloc`, zero-filled),
  `if`, `if (x == NULL)`, `while`, `__unsafe { }`, and `call` of an inline
  body that can write any variable (so every variable acts as a global).
- **Semantics** (`exec`, with fuel): checked accesses trap, signed arithmetic
  wraps, division by zero and `INT_MIN / -1` trap. Inside `__unsafe` the checks
  are off and the same violations are UB. Reading an uninitialized local, an
  out-of-block access, and using an integer as a pointer are all UB.
- **Typing** (`HasTy`, `HasTyS`): flow-sensitive facts for "definitely
  initialized" and "narrowed to nonnull". The rules: nonnull by default;
  narrowing after a null check; reads of possibly uninitialized locals
  rejected; arithmetic and indexing only through nonnull pointers; `void *` to
  `T *` rejected except for fresh allocation; nonnull converts to nullable
  only at the top level; a `while` body must keep the loop's facts; a weakening
  rule.
- **Rule switches** (`Rules`): `nullTrap`, `zeroNonnull`, `castAny`,
  `callKeepsNarrowing`, `ntBounded`. `Rules.current` is the state after the
  review's fix for hole 1. `Rules.preReview` is the state before it.

## What is not modeled

In this core model: free and lifetimes (those are in the ownership model, see
`OWNERSHIP.md`), struct layout and byte-level representation (one cell per
element), function pointers, variadics, `realloc`, uninitialized heap memory
(`alloc` zero-fills), and the system-header and driver-flag issues. `counted n`
has a fixed `n`. Safe C reads the count from a variable, which is the same once
that variable cannot change while the pointer is live (and the model does not
check that it cannot). A callee sees all variables, so the call rule is the
conservative version (see finding 3). The theorem covers programs with no
`__unsafe` block at all. It says nothing about what an `__unsafe` block may do
to memory the safe code later reads.

## The theorem

```lean
def Rules.Sound (R : Rules) : Prop :=
  (R.nullTrap = true ∨ (R.zeroNonnull = false ∧ R.callKeepsNarrowing = false)) ∧
  R.castAny = false ∧ R.ntBounded = true

theorem safe_c_sound (R : Rules) (hR : R.Sound) (D : Decls) (s : Stmt) (G : Facts)
    (hT : HasTyS R D false Facts.empty s G) (hu : s.noUnsafe = true) (fuel : Nat) :
    exec R false fuel (fun _ => .uninit) [] s ≠ .ub
```

`safe_c_sound'` gives the same result as a disjunction: `ok`, `trap`, or
`outOfFuel`. The proof is a preservation argument over an invariant. Every
initialized variable conforms to its declared type, and every narrowed one is
non-null when null accesses are UB (`InvE`). Every heap cell conforms to its
block's type (`WFMem`). Memory only grows, and each block keeps its type and
length (`Ext`). The proof is by induction on fuel, then on the typing
derivation.

**Admitted lemmas: none.**

`Rules.proposed` (current plus `castAny := false`, `ntBounded := true`) and
`Rules.staticNull` (no null trap, with both static null fixes) are both proven
`Sound`. `need_cast`, `need_bounded`, `need_zero` and `need_call` take a sound
rule set, relax one conjunct, and give a well-typed program that reaches UB.
So each conjunct is necessary, not just enough for the proof. As a gate audit,
relaxing each conjunct in `Sound.lean` breaks the proof: at the cast case of
`evalE_safe`, at `addr_safe`/`step_safe`/`convV_safe`, and at `alloc` and
`call` in `exec_safe`.

## Findings

**1. Zero-filled nonnull pointer slots.** The original rule is unsound
(`hole1_ub_preReview`): `int *_Nonnull slot[1]` is zero-filled, and reading
and dereferencing it is UB. The current fix (every null access traps) is
sound in the model, and makes "nonnull" carry no proof obligation at all
(`conf` lets null inhabit nonnull types when `nullTrap`). The static
alternative is `zeroNonnull := false`, which rejects zero-filled storage with a
nonnull pointer element type. It is also sound (`staticNull_sound`), but only
together with fix 3. On its own it rejects every nonnull-element array, since
the model has no "stored before read" tracking for heap slots. A usable static
rule needs that tracking. The trap is the pragmatic fix. Keep it unconditional:
an optimizer that drops the check because the type says nonnull brings the
hole back.

**2. Type confusion through casts and unions.** Unsound under the current rules,
null trap or not. 2a (`hole2a_ub`): an `int` cell is reinterpreted as `int *`
and dereferenced (the union case). 2b (`hole2b_ub`): `int **` to `char *`, a
byte store, then back to `int **`. 2c (`hole2c_ub`): a cast that only changes
the pointee's nullability (`int *_Nonnull *` to `int *_Nullable *`) stores
null into a slot that the original pointer reads as nonnull. That one is UB
only without the null trap: with it, it becomes a trap. The proof's cast
rule is `castAny := false`: a pointee cast is allowed only to `void`, between
equal types, or between two pointer-free types. Nullability and kind count as
part of the type at every level below the top. `hole2_casts_rejected` shows
all three casts are refused. Unions holding pointers need the same rule:
reading a member as a pointer type other than the one last stored is the 2a
cast. A read-only `const char *` view of pointer-holding memory would also be
sound, but it is not modeled.

**3. Null narrowing across a call.** The original rule is unsound
(`hole3_ub_preReview`): `if (g) { clear(); *g; }`. Under the current rules this
is a trap, not UB (`hole3_trap_current`), because the hole-1 fix checks every
null access. So the narrowing is still false, but it is no longer exploitable.
For narrowing to mean something (no null trap), the rule must drop narrowing
facts at a call (`callKeepsNarrowing := false`; `need_call` shows it is
required). The model drops all of them. The precise version drops only facts
about globals and escaped locals. That needs a frame lemma over the callee's
writes, which this model does not have, because its callee sees every variable.

**4. Checked conversion to null-terminated, then an alias erases the
terminator.** Unsound under the current rules (`hole4_ub_current`). A
`char[2]` holding `"a"` converts fine. A store through the indexable alias
overwrites the 0. A walk through the thin null-terminated pointer then reads
past the block. No invariant about the cell contents survives stores through
aliases. The fix (`ntBounded := true`) keeps the bound found by the scan in the
null-terminated pointer (`hi = j + 1`) and checks every access and `++`
against it. The same walk then traps (`hole4_trap_bounded`,
`hole4_trap_proposed`). Without that bound, the only other sound options are
alias control (no writable alias to a buffer while a null-terminated view of it
is live) or a re-scan at every use.
