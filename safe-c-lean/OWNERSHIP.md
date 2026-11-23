# Ownership in safe-c-lean (milestone M6 design proof)

A Lean model of owned and borrowed pointers for `-fsafe-c`: heap allocation,
`free`, `realloc`, owned struct fields, and per-function checking with
`lifetimebound` returns. It has a soundness theorem for the full rule set and,
for every rule, a program that passes the checker without that rule and
faults.

```
lake build      # green, no sorry, no warnings
```

## Files

- `SafeC/Own.lean`: syntax, the big-step semantics, the `Rules` switches and
  the checker (`chkS`, `fnOK`, `Prog.ok`).
- `SafeC/OwnSound.lean`: the invariant, the proof, and the main theorem.
- `SafeC/OwnHoles.lean`: one counterexample per rule, and six C idioms run
  under the full rules. All checked by `decide` in the kernel.

## The theorem

```lean
theorem own_sound (P : Prog) (hP : P.ok Rules.full = true) (D : Decls) (s : Stmt) (G : Ctx)
    (hc : chkS Rules.full P D Ctx.empty s = some G) (fuel : Nat) :
    (exec P fuel (fun _ => .uninit) [] s).isUB = false

theorem own_no_uaf ... :
    (exec P fuel (fun _ => .uninit) [] s).ubKind ≠ some .useAfterFree ∧
    (exec P fuel (fun _ => .uninit) [] s).ubKind ≠ some .doubleFree
```

If every function passes `fnOK` and the main statement passes the checker,
execution never reaches any UB state, for any fuel: not use after free, not
double free, and not the other UB kinds either (reading an uninitialized
variable, a field that does not exist, an interior `free`). What is left is
`ok`, `trap` (null access, out of bounds, division by zero, negative size) or
`outOfFuel`. `#print axioms` lists only `propext`, `Classical.choice` and
`Quot.sound`. `idiom1_sound` applies the theorem to the linked list program
for all fuel, so the hypotheses are satisfiable by real code.

### Semantics

Blocks are never reused. `free` and `realloc` clear a block's `live` flag and
leave it in the list, so a stale pointer is detectable:

- any access or `realloc` through a pointer to a dead block is
  `ub useAfterFree`;
- `free` of a dead block is `ub doubleFree`;
- a borrow that outlives its owner (leak of a borrow) is not an error until it
  is used, as in C; its use is `ub useAfterFree`.

Leaks are allowed. Memory safety here does not include leak freedom.

### Proof idea

The invariant carries a ghost owner map: each block is owned by a variable of
some call frame, written `(depth, x)`, by an owned field `(block, element,
field)`, or by nobody. A block's region is the variable at the top of its
owner chain (`Anc`). Chains are followed by an inductive predicate, so cycles
or stale entries in the map are harmless: they reach no region.

`Inv` says: every block that has a region is live and well formed, and each of
its owned fields points at a block that names that field as its owner; every
live owner variable points at a block it owns; every live borrow points into
the region of its root. Regions are unique per block (`Anc.det`), so a block
can die only when the loans of its one region end. The only blocks that ever
die are those held by a live owner variable of the current frame, and the
checker ends that owner's loans at that point.

Calls need no frame-specific memory reasoning. The callee runs at depth
`d + 1`. Its owned parameters take over the regions of the moved arguments,
and its borrowed parameters keep the caller's regions (through `θ`, which maps
each `ext p` root to the caller's region of that argument). `Frame` says a
statement changes owners only at blocks whose region is at the current depth.
That alone keeps every suspended caller's regions intact across the call.

## Rules, and what each counterexample shows

Each rule is a field of `Rules`. `Rules.full` has them all. For each,
`OwnHoles.lean` gives a program that passes the checker with only that rule
off, then runs it in the kernel to the named UB. With the rule on, the checker
rejects the same program.

| Rule | Counterexample | Program | UB |
|---|---|---|---|
| `moveKills` | `need_moveKills` | `q = p; free(q); free(p);` | double free |
| `freeKills` | `need_freeKills` | `r = p; free(p); r->v;` | use after free |
| `takeKills` | `need_takeKills` | `r = x->next;` then take `x->next` out, free it, read `r->v` | use after free |
| `reallocKills` | `need_reallocKills` | pointer into a growable array held across `realloc` | use after free |
| `ownOpsNeedOwner` (free) | `need_owner_free` | `r = p; free(r); free(p);` | double free |
| `ownOpsNeedOwner` (take) | `need_owner_take` | callee takes and frees `p->next` through a borrowed `p` while the caller holds `x->next` | use after free |
| `argsConsume` | `need_argsConsume` | callee frees its owned parameter, caller frees again | double free |
| `argsAfterMoves` | `need_argsAfterMoves` | `f(&*x, x)`: callee frees the owned one, reads the borrowed one | use after free |
| `retBound` | `need_retBound` | `r = id(&*p); free(p); r->v;` with `id` not `lifetimebound` | use after free |

`need_owner_take` is where aliasing xor mutation shows up: a callee cannot see
the caller's loans, so changing ownership through a borrow needs that borrow
to be exclusive. The model forbids it outright: ownership changes go through
an owner variable only. Storing integers through a borrow is allowed.

## Idioms under the full rules (`OwnHoles.lean`)

1. Singly linked list: push, traverse, pop, free all (`idiom1_list`). The usual
   C pop, `n = head; head = n->next; free(n);`, is rejected: it copies an owned
   field without nulling it (`idiom1_c_pop_rejected`). Write it as a take.
2. Doubly linked list: no pointer form. Use indices into one owned array
   (`idiom2_indices`).
3. Growable array: hold an index across the push and borrow again after
   (`idiom3_index`). Holding a pointer across it is rejected.
4. Arena: many borrows of one owner; reset (`realloc` to 0) ends them all
   (`idiom4_arena`).
5. A parser holding `const char *input`: as a local or a parameter, not as a
   struct field (`idiom5_parser`). Borrows in the heap are excluded (below).
6. Out-parameter as an owned return (`_Owned T *make(void)` for
   `void make(T **out)`), and a callback with a context pointer, both receiving
   borrows for the call only (`idiom6_out_and_callback`).

Plus `lifetimebound_ok`: `node *second(node *l __lifetimebound)` returns
`l->next`, the caller may use it while the list lives and is rejected after
the list is freed.

## Restrictions (what the model leaves out)

- **No borrows in the heap or in globals.** Struct fields are integers or
  owned pointers. A borrow stored in a struct could outlive its owner, and
  struct types have no lifetime parameters to prevent it. Idiom 5 is the cost.
- **Ownership changes only through a local owner variable.** `free`, `realloc`,
  storing into an owned field and taking out of one all need an `_Owned`
  local, not a borrow and not a nested field path. To restructure deeper, take
  the child out into a local first.
- **Loans end per owner, not per path.** Taking out of any owned field of `y`,
  storing into one, `free(y)` and `realloc(y)` end every borrow rooted at `y`,
  even borrows into parts that did not change.
- **One root per borrow.** A borrow depends on exactly one local owner or one
  borrowed parameter. A returned borrow comes from exactly one
  `lifetimebound` parameter, which must itself be a borrow.
- **Owned pointers point at element 0.** `owner + i` is a borrow.
- **Callees see only their parameters.** No globals, no function pointers;
  callbacks are calls to known functions. Recursion is allowed.
- **Loops**: one round of fixpoint iteration; a second pass of the body must
  keep the loop's facts. Branch merges keep a variable live only if both sides
  have it live with the same root.
- **Allocation zero-fills** (`calloc`), and `realloc` zero-fills new elements.
  Uninitialized heap is the core model's open gap, not this one's.
- **No layout**: one cell per field, no casts, no unions, single-threaded.
- **Function well-formedness** (proof convenience, both implied in practice):
  parameter names are distinct, and the `lifetimebound` parameter has borrow
  type. `fnOK` checks both.

## What clang must enforce (M6 spec)

Types: `_Owned T *` is an owning pointer (nullable). A plain `T *` in safe
code is a borrow. The checker is flow-sensitive and per function, with a set
of live variables and, for each borrow, its root (a local owner, or the
borrowed parameter it came from).

1. **Move.** `q = p` between owners moves: `p` is dead until reassigned.
2. **Reassigning an owner ends its loans.** Any assignment to an owner
   (`malloc`/`calloc`, move, take, `NULL`, a call result) ends every borrow
   rooted at it.
3. **`free(x)`** requires `x` to be an `_Owned` local. Afterwards `x` is dead
   and every borrow rooted at `x` is dead.
4. **`realloc(x, n)`** requires an `_Owned` local, is written `x = realloc(x,
   n)`, and ends every borrow rooted at `x`.
5. **Owned fields.** Reading an owned field (`r = y->next`) gives a borrow
   with `y`'s root, never an owner. Storing into one (`y->next = z`) needs `y`
   to be an `_Owned` local, moves `z`, and ends `y`'s loans. Taking one out
   (`x = y->next; y->next = NULL;` as one checked idiom, or a builtin) needs
   `y` to be an `_Owned` local and ends `y`'s loans.
6. **Borrows.** `r = y + i` and `r = &y[i]` take `y`'s root. A borrow is usable
   only while its root's loans are live. Integer stores through a borrow are
   allowed; free, realloc and owned-field writes through a borrow are errors.
7. **Calls.** An owner passed to an `_Owned` parameter is moved; one call
   cannot move the same owner twice. Borrow arguments are checked after all of
   that call's moves: a borrow of an owner moved in the same call is an error.
   Borrow parameters are valid for the call only.
8. **Returned borrows.** A function returning a borrow must mark exactly one
   borrow parameter `lifetimebound`, and the returned value must be derived
   from it. The caller gives the result the root of that argument. A borrow
   return without `lifetimebound` is an error.
9. **No borrows in fields or globals.** A struct field or global of plain
   pointer type is rejected in safe code (or must be `_Owned`).
10. **Joins.** At an if/else merge a variable stays live only if both branches
    leave it live with the same root. At a loop, the state at the head is what
    entry and one pass agree on, and a second pass must keep it.
11. **Definite initialization** before use (already in `-fsafe-c`).
12. Distinct parameter names; `lifetimebound` only on a borrow parameter.
