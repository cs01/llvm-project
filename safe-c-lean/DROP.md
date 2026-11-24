# `_Drop` cleanup in safe-c-lean

A Lean model of `_Drop_free` locals (`clang/lib/Headers/safec.h`):

```c
{ int *p _Drop_free = malloc(4); ... }   // free(p) runs at every exit from the block
```

Blocks that declare a `_Drop_free` pointer initialized by `malloc`, plain
pointer locals that copy it, `_Take`, moves by name into an owner, `free`,
dereference, `if`, sequencing, and the early exits `break` and `return`.
It has a soundness theorem for the full rule set and, for every rule, a
program that passes the checker without that rule and reaches UB.

```
lake build      # green, no sorry, no warnings
```

## Files

- `SafeC/Drop.lean`: syntax, the big-step semantics, the `Rules` switches and
  the checker (`chk`, `check`).
- `SafeC/DropSound.lean`: the invariant, the proof, the main theorem, and
  `#print axioms drop_sound`.
- `SafeC/DropHoles.lean`: one counterexample per rule and the accepted idioms,
  all checked by `decide` in the kernel; `idioms_sound` applies the theorem to
  the idioms for all fuel.

## The theorem

```lean
theorem drop_sound (s : Stmt) (r : Res) (hc : check Rules.full s = some r) (fuel : Nat) :
    (exec fuel St.init s).isUB = false
```

If the checker accepts `s` under the full rules, running it from the initial
state (every pointer null, nothing allocated) never reaches UB, for any fuel.
The UB kinds are `useAfterFree` (dereferencing a freed block), `doubleFree`
and `invalidFree` (freeing the address of a local). What is left is `ok`,
`brk` or `ret` at the top level, `trap` (a null dereference) or `outOfFuel`.
`#print axioms` lists only `propext`, `Classical.choice` and `Quot.sound`.

### Semantics

A pointer is null, a heap block numbered in allocation order, or the address
of a local. `block x body` is `do { int *x _Drop_free = malloc(n); body }
while (0)`: it allocates a fresh block into `x`, runs the body, and on each of
fallthrough, `break` and `return` runs the cleanup `free(x)` (a `return` then
goes on outward through the enclosing cleanups). That every exit runs the
cleanup is part of the semantics, not a rule; the proof covers each exit.

### Proof idea

The checker gives each pointer local one of: null; `own` (null or a live block
no other owner holds); a borrow of owner `x` (the same value as `x`); a
local's address; dead. It returns three states: at fallthrough, at `break`,
at `return`. The invariant `Inv` counts every `_Drop` variable in scope as an
owner whatever its abstract value, because its cleanup will free it. Owners
hold null or live blocks, no two owners hold the same block, and a borrow
holds its owner's value. So when an owner's block is freed, by name or by a
cleanup, no other owner holds it, and every borrow of it is killed in the
same step; everything still usable stays live.

## Rules, and what each counterexample shows

Each rule is a field of `Rules`; `Rules.full` has them all. For each,
`DropHoles.lean` gives a program that passes the checker with only that rule
off, then runs it to the UB named. With the rule on, it is rejected.

| Rule | Counterexample | Program | UB |
|---|---|---|---|
| `noMoveByName` | `need_noMoveByName` | `{ int *p _Drop_free = malloc(4); int *_Owned q = p; } free(q);` | double free |
| `copiesDie` | `need_copiesDie` | hole 18: `{ int *p _Drop_free = malloc(4); keep = p; } *keep;` | use after free |
| `noManualFree` | `need_noManualFree` | `{ int *p _Drop_free = malloc(4); free(p); }` | double free |
| `dropHoldsOwner` | `need_dropHoldsOwner` | `{ int *p _Drop_free = malloc(4); p = &a; }` | invalid free |
| `dropHoldsOwner` (borrow) | `need_dropHoldsOwner_borrow` | `{ int *q _Drop_free = malloc(4); { int *p _Drop_free = malloc(4); p = q; } }` | double free |

`dropHoldsOwner` is not in the compiler's list. It is the rule that a
`_Drop_free` variable only ever holds an owner: a `malloc` result, a `_Take`,
or a move from an owner. The compiler rejects the heap case
(`need_dropHoldsOwner_borrow`, `p = q`) through its lifetime analysis, which
sees the cleanup as a use of `q`'s allocation. It has no such check for a
pointer that is not a heap allocation. The compiler built on 2026-11-24
(`llvm-safe-c/build.noindex`, `-fsafe-c -fsyntax-only -include safec.h`)
accepts all three of these with no diagnostic:

```c
void assign_local(void) {
  int a = 0;
  int *_Nullable p _Drop_free = malloc(4);
  p = &a;                              // the cleanup calls free(&a)
}
void init_local(void) {
  int a = 0;
  int *_Nullable p _Drop_free = &a;    // the cleanup calls free(&a)
}
void init_param(int *q) {
  int *_Nullable p _Drop_free = q;     // frees the caller's pointer
}
```

`init_param` is a double free in any caller that owns what it passes. The fix
the model proves: an assignment or initializer of a `_Drop_free` variable
must be an owned value (allocation, `_Take`, a move from an `_Owned` local);
a plain borrow or an address is rejected.

## Idioms under the full rules

- `take_keep_ok`: h18_take, `keep = _Take(p)` inside the block, then `*keep`
  and `free(keep)` after it.
- `take_free_ok`: `free(_Take(p))` inside the block, as in `buf_free`.
- `borrow_in_scope_ok`: a copy used while the variable is in scope.
- `early_return_ok`: `if (c) return;` before the `_Take`; the cleanup frees
  `p` on the return path.
- `break_ok`: `if (c) break;` out of the block with a live copy.
- `break_copy_rejected`: hole 18 on the `break` path, rejected.
- `cond_take_ok`: a `_Take` on one branch only; after the block `keep` is null
  or owns the block, and `free(keep)` is accepted.
- `nested_ok`: nested `_Drop` blocks; the inner exit ends only borrows of the
  inner variable, a borrow of the outer one lives until the outer exit.
- `nested_late_rejected`: that borrow used after the outer exit, rejected.

## What the model does not cover

- **Only `_Drop_free`.** `_Drop(fn)` with a user cleanup over a struct is not
  modeled; the cleanup here is `free` of a pointer.
- **No shadowing.** A block may not redeclare a `_Drop` variable already in
  scope; C programs that shadow are modeled with distinct names.
- **One owner per block, no aliasing owners.** Plain copies are borrows tied
  to one owner; a write to, free of, move from or `_Take` of an owner, and the
  end of a `_Drop` scope, end its borrows. This generic ownership part is what
  `Own.lean` models in detail; here it is built in, not a rule.
- **Returns carry no value.** Returning `p` by name is a move to the caller,
  the same check as `move`; `return _Take(p)` is `take`.
- **No loops**, no `goto`, no conditions the checker reads; `break` leaves
  the innermost block (the `do ... while (0)` around it). `malloc` never fails.
- **Leaks are not UB**: overwriting an owner, or a `_Take` into an owner that
  holds a block, leaks it.
