# `_Err_or` pointers in safe-c-lean

A Lean model of the kernel `ERR_PTR` idiom under Safe C's planned `_Err_or`
annotation (`safe-c-notes/kernel.md` section 1):

```c
struct file *_Err_or f = filp_open(path);   /* a pointer, or an errno as a pointer */
if (IS_ERR(f)) return PTR_ERR(f);           /* PTR_ERR only where IS_ERR is known */
f->pos = 0;                                 /* deref only where !IS_ERR is known */
```

Pointer locals that hold null, a valid pointer, or an error pointer; calls to
`_Err_or` (and `_Nullable _Err_or`) functions, `ERR_PTR`, `&obj`, copies,
dereference, passing to a plain parameter, `PTR_ERR`, conditions `IS_ERR`,
`IS_ERR_OR_NULL` and `== NULL` that the checker reads, opaque `if`,
sequencing and early `return`. It has a soundness theorem for the full rule
set and, for every rule, a program that passes the checker without that rule
and reaches UB.

```
lake build      # green, no sorry, no warnings; the Err files build in under 1 s
```

## Files

- `SafeC/Err.lean`: syntax, the big-step semantics, the `Rules` switches and
  the checker (`chk`, `check`).
- `SafeC/ErrSound.lean`: the invariant, the proof, the main theorem, and
  `#print axioms err_sound`.
- `SafeC/ErrHoles.lean`: one counterexample per rule and the accepted idioms,
  checked by `decide` (or `rfl` for idioms stated for every callee result);
  `idioms_sound` applies the theorem to them for all fuel.

## The theorem

```lean
theorem err_sound (s : Stmt) (r : Res) (hc : check Rules.full s = some r) (fuel : Nat) :
    (exec fuel St.init s).isUB = false
```

If the checker accepts `s` under the full rules, running it from the initial
state (every pointer local null) never reaches UB, for any fuel and whatever
each callee returns. The UB kinds are `errDeref` and `nullDeref`
(dereferencing an error pointer or null, directly or by a callee through a
non-`_Err_or` parameter) and `notErr` (`PTR_ERR` of a pointer that is not an
error). What is left is `ok`, `ret`, `trap` or `outOfFuel`. `#print axioms`
lists only `propext`, `Classical.choice` and `Quot.sound`.

### Choices

- **Null dereference is UB**, not a trap (the `_Drop` model traps on null).
  Safe C rejects a `_Nullable` dereference, and the `isErrKeepsNull` hole is
  a null dereference, so it has to count.
- **`PTR_ERR` of a non-error is UB.** It is a wrong value, not a memory error:
  `PTR_ERR(NULL)` is 0, so the function reports success with no object. The
  rule promises it never happens, so the model counts it.
- **`_Err_or` is orthogonal to nullability.** `T *_Err_or` is a valid pointer
  or an error; `T *_Nullable _Err_or` may also be null. A callee that breaks
  its non-null contract is `trap`, not this function's UB.
- The callee's result is part of the syntax (`call x nullable v`, as `branch`
  carries its condition); the checker never reads `v`.

### Proof idea

The checker keeps, per local, the set of kinds it may hold (null, error,
valid) and an optional alias record "holds the same value as `t`", the null
analysis's alias map with chains flattened. The invariant: each local's
value is of an allowed kind, and each alias record is true. A test of `x`
narrows `x`, its alias target and every local aliasing that target, all of
which hold `x`'s value, so the narrowed kinds still fit. A write to `y` drops
every record naming `y`. Joins take the union of kinds and keep an alias
record only if both sides have it.

## Rules, and what each counterexample shows

Each rule is a field of `Rules`; `Rules.full` has them all. For each,
`ErrHoles.lean` gives a program that passes the checker with only that rule
off, then runs it to the UB named. With the rule on, it is rejected.

| Rule | Counterexample | Program | UB |
|---|---|---|---|
| `derefNeedsCheck` | `need_derefNeedsCheck` | `f = filp_open(); f->pos = 0;` | error deref |
| `joinKeepsMaybe` | `need_joinKeepsMaybe` | `f = filp_open(); if (c) { if (IS_ERR(f)) return; } f->pos = 0;` | error deref |
| `copyKeepsKind` | `need_copyKeepsKind` | `f = filp_open(); g = f; g->pos = 0;` | error deref |
| `copyKeepsKind` (re-assign) | `need_copyKeepsKind_reassign` | `if (IS_ERR(f)) return; f = filp_open(); f->pos = 0;` | error deref |
| `aliasDiesOnWrite` | `need_aliasDiesOnWrite` | `g = f; f = filp_open(); if (IS_ERR(f)) return; g->pos = 0;` | error deref |
| `paramNeedsCheck` | `need_paramNeedsCheck` | `f = filp_open(); use(f);` | error deref in callee |
| `isErrKeepsNull` | `need_isErrKeepsNull` | `f = lookup(); if (IS_ERR(f)) return; f->pos = 0;` (`lookup` may return NULL) | null deref |
| `ptrErrNeedsErr` | `need_ptrErrNeedsErr` | `f = lookup(); if (IS_ERR_OR_NULL(f)) return PTR_ERR(f);` | `PTR_ERR(NULL)` = 0 |

`aliasDiesOnWrite`, `isErrKeepsNull` and `ptrErrNeedsErr` are not in the
kernel.md list.

- `aliasDiesOnWrite`: the null analysis already does this
  (`invalidateAliasesFor` in `forgetFactsAbout`); the error fact must go
  through the same alias map, not a new one that forgets it.
- `isErrKeepsNull` is the hole a straightforward build on the null analysis
  would have. Treating `!IS_ERR(p)` like `p != NULL` in the narrowing code
  marks `p` non-null, but `IS_ERR(NULL)` is false. `!IS_ERR(p)` must clear
  only the error fact; `!IS_ERR_OR_NULL(p)` clears both. kernel.md's
  "`IS_ERR_OR_NULL` narrows both" is the permissive half of the rule; it has
  no hole of its own, since without it the checker only rejects more.
- `ptrErrNeedsErr` needs a positive fact ("is an error here"), not only the
  "may be an error" fact the narrowing tracks. In this model it is "may be
  neither null nor valid". `IS_ERR_OR_NULL` true does not give it, which is
  the `PTR_ERR_OR_ZERO` bug class.

## Idioms under the full rules

- `early_return_ok`: `if (IS_ERR(f)) return PTR_ERR(f);` then a dereference
  and a call with `f`.
- `if_else_ok`: `PTR_ERR` on the `IS_ERR` branch, dereference on the other.
- `or_null_ok`: `if (IS_ERR_OR_NULL(f)) return;` on a nullable `_Err_or`.
- `err_then_null_ok`: `IS_ERR` then `if (!f) return;` on a nullable one.
- `copy_check_ok`: `g = f; if (IS_ERR(g)) return;` then both `*g` and `*f`.
- `mk_err_ok`: `f = ERR_PTR(-ENOMEM); return PTR_ERR(f);`
- `wrong_pointer_rejected`: testing `g` does not narrow `f`.
- `is_err_nullable_rejected`: `IS_ERR` alone on a nullable `_Err_or`.

## What the model does not cover

- **No lifetimes.** A valid pointer stays valid; freeing is `Drop.lean`'s and
  `Own.lean`'s concern.
- **Conditions only as `if (T(x))`.** No `bool bad = IS_ERR(f);` guards (the
  null analysis's `BoolGuards`, which must die when `f` is written), no
  `&&`/`||` conditions, no `unlikely()`, no `WARN_ON(IS_ERR(f))`.
- **No struct fields, globals or pointers to pointers.** Only locals; member
  paths (`s->f`) need the same rules on `NarrowedMembers` and
  `MemberAliases`.
- **No `PTR_ERR_OR_ZERO`, `ERR_CAST`, returns of `_Err_or` values** or
  `_Err_or` parameters; returning or passing an unchecked `f` to an
  `_Err_or` destination is a copy, which `copyKeepsKind` covers.
- **No loops**, no `goto`. Early `return` carries no value.
