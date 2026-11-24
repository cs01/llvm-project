# Ownership in safe-c-lean (M2 design proof)

A Lean model of the four pointer kinds of the M2 design
(`safe-c-notes/m2-ownership.md`): owned, borrow, exclusive borrow and
unchecked pointers, plus heap allocation, `free`, `realloc`, owned struct
fields, stack locals with scopes, struct assignment, globals and views. It has
a soundness theorem for the full rule set and, for every rule, a program that
passes the checker without that rule and reaches UB.

```
lake build      # green, no sorry, no warnings
```

## Files

- `SafeC/Own.lean`: syntax, the big-step semantics, the `Rules` switches and
  the checker (`chkS`, `fnOK`, `Prog.ok`).
- `SafeC/OwnSound.lean`: the invariant, the proof, and the main theorem.
- `SafeC/OwnHoles.lean`: one counterexample per rule, the C idioms and the
  M2 idioms run under the full rules. All checked by `decide` in the kernel.

## The theorem

```lean
theorem own_sound (P : Prog) (hP : P.ok Rules.full = true) (D : Decls) (s : Stmt) (G : Ctx)
    (hc : chkS Rules.full P D Ctx.empty s = some G) (fuel : Nat) :
    (exec P fuel (fun _ => .uninit) [] s).isUB = false

theorem own_no_uaf ... :
    (exec P fuel (fun _ => .uninit) [] s).ubKind ≠ some .useAfterFree ∧
    (exec P fuel (fun _ => .uninit) [] s).ubKind ≠ some .doubleFree ∧
    (exec P fuel (fun _ => .uninit) [] s).ubKind ≠ some .danglingStack
```

If every function passes `fnOK` and the main statement passes the checker,
execution never reaches any UB state, for any fuel: not use after free, not
double free, not a dangling stack access, and not the other UB kinds either
(reading an uninitialized variable, a field that does not exist, an interior
`free`). What is left is `ok`, `trap` (null access, out of bounds, division by
zero, negative size) or `outOfFuel`. `#print axioms` lists only `propext`,
`Classical.choice` and `Quot.sound`. `idiom1_sound`, `grow_sound` and
`globals_sound` apply the theorem to concrete programs for all fuel, so the
hypotheses are satisfiable by real code.

"Outside `__unsafe`" is built into the checker: it has no unsafe block and
rejects every dereference of an unchecked pointer. A program with an
`__unsafe` block is outside the theorem.

### Semantics

Blocks are never reused. `free`, `realloc` and the end of a scope clear a
block's `live` flag and leave it in the list, so a stale pointer is
detectable:

- any access or `realloc` through a pointer to a dead heap block is
  `ub useAfterFree`; to a dead stack block, `ub danglingStack`;
- `free` of a dead block is `ub doubleFree`;
- a borrow that outlives its owner is not an error until it is used, as in C.

Leaks are allowed. Memory safety here does not include leak freedom.

### What is modeled, and how

| M2 feature | Model |
|---|---|
| owned `T *_Owned` | `VTy.own s`, `FTy.own s` |
| borrow `T *` | `VTy.ref s`; `FTy.ref s` exists only for counterexamples |
| exclusive `T *_Exclusive` | `VTy.excl s`: a parameter, or a stack local |
| unchecked `T *_Unchecked` | `VTy.unc s`, `FTy.unc s`; `ucast`, `pstore` |
| stack local whose address is taken | `scope x s n body`: `x` is `excl`, its block dies at the end |
| struct assignment `*x = *y` | `copy x y` |
| globals | one struct `G`: main holds it as a stack local, every function that touches a global takes it as an exclusive parameter |
| view structs | scalar replacement: each view field is a borrow local or borrow parameter |

An exclusive borrow is modeled as a **lent owner**. At the call the region of
the lent variable moves to the callee's parameter (as for an owned argument);
at the return it moves back (`backPar`). In between the caller's loans of that
variable are dead and the variable cannot be named again in the same call.
The callee may take and put owned fields through it, never free, `realloc`,
move or reassign it, and must return with it live. That is what lets the
caller get it back: `exec_preserves` shows an unassigned variable keeps its
value, so after the call the parameter still points at the block the caller
lent.

Globals need no new semantics under this reading. "Any call ends borrows
derived from globals" is the lend of `G` ending the caller's loans of `G`; a
pure callee is one that does not take `G`, so a borrow of a global survives a
call to it (`globals_pure_impure`).

### Proof idea

The invariant carries a ghost owner map: each block is owned by a variable of
some call frame, written `(depth, x)`, by an owned field `(block, element,
field)`, or by nobody. A block's region is the variable at the top of its
owner chain (`Anc`). Owners and exclusive borrows both head regions, which is
why the proof treats them alike except where a statement frees, moves or
reassigns.

`Inv` says: every block that has a region is live and well formed, and each of
its owned fields points at a block that names that field as its owner; every
live owner or exclusive variable points at a block it heads; every live borrow
points into the region of its root; unchecked variables hold some pointer.
Regions are unique per block (`Anc.det`), so a block can die only when the
loans of its one region end.

Calls need no frame-specific memory reasoning. The callee runs at depth
`d + 1`. Its owned and exclusive parameters take over the regions of their
arguments (`calleePar`), and its borrowed parameters keep the caller's regions
(through `θ`). `Frame` says a statement changes owners only at blocks whose
region is at the current depth, which keeps every suspended caller's regions
intact across the call. A scope is a fresh block owned by `x` for the body,
then the same step as `free(x)` (`kill_tail`).

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
| `ownOpsNeedOwner` (take) | `need_owner_take` | callee takes and frees `p->next` through a shared `p` while the caller holds `x->next` | use after free |
| `argsConsume` | `need_argsConsume` | callee frees its owned parameter, caller frees again | double free |
| `argsAfterMoves` | `need_argsAfterMoves` | `f(&*x, x)`: callee frees the owned one, reads the borrowed one | use after free |
| `argsAfterMoves` (globals) | `need_argsAfterMoves_global` | `use(G, &G.d[0])`: callee frees `G.d`, reads the borrow | use after free |
| `retBound` | `need_retBound` | `r = id(&*p); free(p); r->v;` with `id` not `lifetimebound` | use after free |
| `lendKills` | `need_lendKills` | p04: `first = &v.d[0]; grow(&v); *first;`, `grow` reallocates `v->d` through an exclusive borrow | use after free |
| `lendOnce` | `need_lendOnce` | `two(&v, &v)`: callee borrows `q->d`, frees `p->d`, reads the borrow | use after free |
| `noRefFields` (heap) | `need_noRefFields_heap` | p01, x12 heap slot: `a->p = &*b; free(b); a->p->v` | use after free |
| `noRefFields` (global) | `need_noRefFields_global` | q01: `stash(p) { g = p; }`, caller frees `p`, reads `*g` | use after free |
| `noRefFields` (stack) | `need_noRefFields_stack` | s03 `fill`: callee stores `&y` (its stack local) into `s->p`, caller reads it | dangling stack |
| `uncNeedsUnsafe` | `need_uncNeedsUnsafe` | q01 with `g` unchecked: storing is accepted, the dereference `*g` is what is rejected | use after free |
| `stackKills` | `need_stackKills` | x12 scope: `{ int x[64]; q = &x[5]; } *q;` | dangling stack |
| `copyPlain` | `need_copyPlain` | q02: `struct B c = a; drop(&a); drop(&c);` with `drop` freeing `b->p` | double free |

Probe coverage: p01, p04, q01, q02, s03 and x12 are modeled directly above.
q04 (getter then destroy) is `lifetimebound_ok` plus `need_lendKills`: the
getter's result is rooted at `t`, and `destroy(&t)` must take `t` exclusively,
which ends that loan. r15 (`destroy(int **pp) { free(*pp); }`) is an exclusive
borrow of a struct holding an owned pointer: `free(take(&pp->p))`, ending the
caller's loans of `p`. r14 (an array of owners with `arr[1] = arr[0]`) is
rejected by rule 5 (reading an owned field gives a borrow, and `put` needs an
owner). p07 (returning a struct holding `&x`) is a view return: rule 9 rejects
it because the root is a local, not a `lifetimebound` parameter, and rule 13
ends the loan at the scope end.

`need_owner_take` is where aliasing xor mutation shows up: a callee cannot see
the caller's loans, so changing ownership through a borrow needs that borrow
to be exclusive. `need_lendKills` and `need_lendOnce` are the two halves of
what makes an exclusive borrow exclusive: the caller's other loans of it end,
and no other argument of the same call reaches it.

## Idioms under the full rules (`OwnHoles.lean`)

1. Singly linked list: push, traverse, pop, free all (`idiom1_list`). The usual
   C pop, `n = head; head = n->next; free(n);`, is rejected: it copies an owned
   field without nulling it (`idiom1_c_pop_rejected`). Write it as a take.
2. Doubly linked list: indices into one owned array (`idiom2_indices`), or a
   back pointer that is `_Unchecked` and dereferenced in `__unsafe`.
3. Growable array: hold an index across the push and borrow again after
   (`idiom3_index`). Holding a pointer across it is rejected.
4. Arena: many borrows of one owner; reset (`realloc` to 0) ends them all
   (`idiom4_arena`).
5. A parser holding `const char *input`: as a local or a parameter
   (`idiom5_parser`), or in a view struct (scalar replaced, the same thing).
6. Out-parameter as an owned return, and a callback with a context pointer,
   both receiving borrows for the call only (`idiom6_out_and_callback`).
7. p04 the M2 way: `grow(&v)` through an exclusive borrow, then borrow `v.d`
   again (`grow_reborrow_ok`, `grow_sound`).
8. Globals: a borrow of a global survives a pure call and dies at an impure
   one (`globals_pure_impure`, `globals_sound`).

Plus `lifetimebound_ok`: `node *second(node *l __lifetimebound)` returns
`l->next`, the caller may use it while the list lives and is rejected after
the list is freed.

## Restrictions (what the model leaves out)

- **Loans end per root, not per path.** Taking out of any owned field of `y`,
  storing into one, `free(y)`, `realloc(y)` and lending `y` exclusively end
  every borrow rooted at `y`, even borrows into parts that did not change. Two
  exclusive arguments from one owner are rejected even when their paths are
  disjoint (`f(&v.a, &v.b)`); per-path disjointness is not proven.
- **One root per borrow.** A borrow depends on exactly one owner, exclusive
  variable or borrowed parameter. A returned borrow comes from exactly one
  `lifetimebound` parameter, which must itself be a borrow.
- **Exclusive borrows exist only as parameters and stack locals.** No
  exclusive local made from another pointer, no exclusive fields, no
  exclusive return, no lending of an owned field directly (take it out into a
  local first). An owned local may be lent (`f(&*owner)`); a shared borrow
  cannot be upgraded.
- **Ownership changes only through an owner or exclusive variable.** `free`
  and `realloc` need an owned local; `take` and `put` need an owned local or an
  exclusive variable. A nested path (`x->a->b`) is restructured by taking the
  child out first.
- **Views are modeled by scalar replacement.** Nested views, arrays of views
  and pointers to views are not modeled; storing a borrow through a pointer
  to a view is the `noRefFields` hole (`need_noRefFields_stack`).
- **Globals are one struct passed as an exclusive parameter.** The model does
  not compute purity: a function is pure exactly when it does not take `G`.
  Globals are never freed or reassigned as a whole; their owned fields are
  taken and put like any exclusive borrow's.
- **Struct assignment** is accepted only for structs whose fields are
  integers or unchecked pointers; a struct with owned fields is rejected, not
  moved. `copy` assigns element 0 only.
- **Unchecked pointers** can be created from any pointer, copied, stored in
  unchecked fields, and returned. Null tests on them are not modeled, nor is
  any conversion back to a borrow (that needs `__unsafe`).
- **Scopes.** A `scope` local is fresh at entry and must still be live at the
  end; the body may not assign it. Only struct or array locals whose address
  is taken need a scope; other locals are environment values.
- **Owned pointers point at element 0.** `owner + i` is a borrow.
- **Callees see only their parameters.** No function pointers; callbacks are
  calls to known functions. Recursion is allowed.
- **Loops**: one round of fixpoint iteration; a second pass of the body must
  keep the loop's facts. Branch merges keep a variable live only if both sides
  have it live with the same root.
- **Allocation zero-fills** (`calloc`), and `realloc` zero-fills new elements.
  Stack blocks are zero-filled too. Uninitialized memory is the core model's
  open gap, not this one's.
- **No layout**: one cell per field, no casts, no unions, single-threaded.
- **Function well-formedness** (proof convenience, implied in practice):
  parameter names are distinct, the `lifetimebound` parameter has borrow type,
  and every exclusive parameter is unassigned and live at the return. `fnOK`
  checks all three.

## Rules clang must enforce (M2 implementation spec)

Each item is one compiler check. "Owner" means an `_Owned` local; "exclusive
variable" means a `_Exclusive` parameter or an address-taken stack local.
The flow state per function is the set of live variables and, for each
borrow, its root: an owner, an exclusive variable, a borrowed parameter, or
`G` (the globals).

1. **Move.** `q = p` between owned pointers moves `p`: `p` is dead until
   reassigned. Passing an owner to an `_Owned` parameter moves it.
2. **Reassigning an owner ends its loans.** Any assignment to an owner
   (`malloc`/`calloc`, move, take, `NULL`, a call result) ends every borrow
   rooted at it.
3. **`free(x)`** requires `x` to be an owner (not an exclusive variable, a
   borrow, a field or a global). Afterwards `x` and every borrow rooted at `x`
   are dead.
4. **`realloc`** is written `x = realloc(x, n)` with `x` an owner, and ends
   every borrow rooted at `x`.
5. **Owned fields.** Reading an owned field (`r = y->f`) gives a borrow with
   `y`'s root, never an owner. Storing into one (`y->f = z`) needs `y` to be
   an owner or exclusive variable and `z` an owner; it moves `z` and ends
   `y`'s loans. Taking one out (`x = take(&y->f)`) needs `y` to be an owner
   or exclusive variable and ends `y`'s loans. Through a plain borrow all
   three ownership operations are errors.
6. **Borrows.** `r = y + i`, `r = &y[i]` and `r = y->f` take `y`'s root. A
   borrow is usable only while its root's loans are live. Stores of integers
   and other plain data through a borrow are allowed.
7. **Exclusive variables** are never assigned, freed, reallocated, moved, or
   passed to an `_Owned` parameter. A function returns with every exclusive
   parameter still holding the pointer it received.
8. **Calls**, checked in this order:
   1. an argument for an `_Owned` parameter must be a live owner; it is moved;
   2. an argument for a `_Exclusive` parameter must be a live owner or
      exclusive variable (`&*owner`, `&stack_local`, a caller's exclusive
      parameter); it is lent, and every loan of it ends at the call;
   3. a variable may appear at most once among the owned and exclusive
      arguments of one call;
   4. every borrow argument must still be live after steps 1 to 3, so no
      borrow argument may be rooted at a variable moved or lent in the same
      call;
   5. after the call, moved owners stay dead and lent variables are live
      again, with their old loans still dead; all other loans survive;
   6. borrow parameters are `noescape`: valid for the call only.
9. **Returned borrows and views.** A function returning a borrow (or a view)
   marks exactly one borrow parameter `lifetimebound`, and the returned value
   is derived from it. The caller gives the result the root of that argument.
   A borrow return without `lifetimebound` is an error, so a borrow rooted at
   a local can never be returned.
10. **Fields and globals hold no borrows.** A struct field or global of plain
    pointer type is an error unless the struct is `_View`. Fields and
    globals are integers, `_Owned` or `_Unchecked` pointers.
11. **Views** live only in locals and by-value parameters, never in the heap,
    a global, or a non-view struct. Each view field is a separate borrow with
    its own root. A borrow may be written into a view field only by naming a
    local view (`v.f = r`), never through a pointer to a view.
12. **Unchecked pointers.** Any pointer converts to `_Unchecked` (a
    copy, not a move) and may be stored in unchecked fields and globals.
    Outside `__unsafe` an unchecked pointer is never dereferenced, indexed,
    borrowed from, freed, or converted back to a borrow or owner.
13. **Stack addresses.** Taking the address of a local (`&x`, array decay)
    gives a borrow rooted at `x`, and makes `x` an exclusive variable for the
    rest of its scope. At the end of `x`'s scope every borrow rooted at `x`
    ends. Such a borrow may be passed to `noescape` parameters and `x` may be
    lent exclusively; rules 9 and 10 stop every other escape.
14. **Globals.** Globals act as one struct `G` lent exclusively to every
    impure call:
    1. every call to an impure function ends every borrow rooted at `G`;
    2. a borrow rooted at `G` may not be an argument of an impure call
       (rule 8.4);
    3. an owned global is restructured with `take` and `put` like an owned
       field (`free(take(&g))`, not `free(g)`);
    4. a function is impure if it reads or writes a global or calls an impure
       function (transitively); calls to pure functions keep `G`'s loans.
15. **Struct assignment.** `a = b` of a struct type with an owned field
    (directly or nested) is an error. Write the owned fields with `take` and
    `put`.
16. **Joins.** At an if/else merge a variable stays live only if both
    branches leave it live with the same root. At a loop, the state at the
    head is what entry and one pass agree on, and a second pass must keep it.
17. **Definite initialization** before use (already in `-fsafe-c`).
18. Distinct parameter names; `lifetimebound` only on a borrow parameter.

## What the proof says about the M2 design

- **Globals: "any call ends global borrows" is not enough.** A borrow derived
  from a global must also not be passed to an impure callee: the callee can
  free the global's owned field while it holds the borrow
  (`need_argsAfterMoves_global`). Rule 14.2 adds this.
- **Exclusive arguments must be disjoint from shared arguments too**, not only
  from each other: `f(&v, &v.d[0])` with the first exclusive is the same hole
  as `f(&*x, x)` (rule 8.4). "The parent is frozen while the borrow lives" is
  precisely: lending ends the caller's loans (`need_lendKills`) and the lent
  variable cannot be named again in the call (`need_lendOnce`).
- **An exclusive borrow frees owned fields, not itself.** The design table
  lists "free, realloc" as operations through an exclusive borrow. The proof
  needs them restricted to owned fields reached through it (via `take`); the
  pointer itself is never freed, reallocated or reassigned, or the caller
  could not get it back (rule 7).
- **Views need a write rule.** "Lives only in locals and parameters" does not
  stop `fill(struct S *s) { int y; s->p = &y; }` when `S` is a view behind a
  pointer (s03, `need_noRefFields_stack`). Rule 11 allows borrow writes only
  into a view named as a local.
- **Rule 7 of the design is stricter than needed.** "A local's address may
  flow only into locals of the same or an inner scope" can be relaxed to
  "the end of the local's scope ends every loan rooted at it" (rule 13,
  `need_stackKills`), which is what the flow layer computes anyway: copying
  `&x` into an outer local is fine as long as it is not used after the scope.
- **Struct copies need their own rule.** Design rule 1 covers pointer copies.
  A struct assignment copies every owned field inside it (q02); the model
  rejects it (rule 15). Treating it as a move of the whole source struct is
  plausible but not proven.
