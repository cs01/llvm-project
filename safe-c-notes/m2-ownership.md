# M2: static ownership (design, 2026-11-23)

Goal: use after free, double free, stale pointers after `realloc`, and escaping stack
addresses are compile errors for every pointer, not only locals. Unproven means
rejected, as in Rust's borrow checker. No run-time floor (user decision). The 13 open
audit probes in `safe-c-notes/audit-2026-11-23/` are the acceptance test.

## Pointer kinds

Every pointer type gets one of four kinds. Spelled through `safec.h`; nothing on other
compilers.

| Kind | Spelling | Meaning | Rust analogue |
|---|---|---|---|
| owned | `T *_Owned` | the one pointer responsible for freeing; copying it moves it | `Box<T>` |
| borrow | plain `T *` | uses memory someone else owns, for a bounded time | `&T` / `&mut T` |
| exclusive borrow | `T *_Excl` | a borrow through which ownership may change (free, realloc, owned-field writes) | `&mut T` |
| unchecked | `T *_Unchecked` | may be stored anywhere; dereferencing it needs `__unsafe` | `*const T` |

**Kinds are types** (decided by claude, 2026-11-23): `[[clang::annotate_type("safec_owned")]]`
through `safec.h`, written right after the `*` it describes, so an inner pointer can have
one: `char *_Owned *_Owned items` is a vector of owned strings. A fifth kind,
`_Static` (Rust `&'static`), holds only string literals, globals and functions, so
string tables need no `__unsafe`. Heap memory may hold only kinded pointers.

## Where each kind may live

- **Locals:** any kind.
- **Parameters:** borrows are `noescape` by default: the callee may not store them
  anywhere that outlives the call, or return them unless `lifetimebound`. An owned
  parameter takes ownership (`ownership_takes` is implied).
- **Struct fields and globals:** owned or unchecked pointers only. A plain pointer field
  is an error, except in a struct marked `_View`. A view holds borrows, lives only
  in locals and parameters, and is never stored in the heap or a global.
- **Returns:** owned (`ownership_returns`), or a borrow with a `lifetimebound` parameter.

## Rules (from `safe-c-lean/OWNERSHIP.md`, numbered there)

1. `q = p` between owned pointers moves `p`: it is dead until reassigned (rule 1).
2. Only an owned local, or an owned field reached through an exclusive borrow, can be
   freed, reallocated, or passed to an owned parameter (rules 3, 4, 7).
3. Reassigning, freeing or reallocating an owner, or writing an owned field, ends every
   borrow derived from it (rules 2, 5).
4. A borrow from a field (`n->next` read into a plain `T *`) is rooted at the path's
   owner and dies with it (rule 6).
5. Two arguments of one call that are exclusive borrows must be disjoint paths, and an
   exclusive borrow freezes its parent while it lives (rule 7).
6. Taking a pointer out of an owned field is a move: `x = take(&n->next)` (a `safec.h`
   helper that returns the owned value and nulls the field) (rule 5, the take idiom).
7. A local's address may flow only into locals of the same or an inner scope and into
   `noescape` parameters (audit finding 4).

## Corrections from the Lean proof (2026-11-23)

The extended model (`safe-c-lean/OWNERSHIP.md`, 18 rules, `own_sound` re-proved) showed
the rules above are not enough as written:

1. A borrow of a global must not be passed to a callee that may free the global's
   fields; ending such borrows at every call is not enough.
2. An exclusive argument must be disjoint from every other argument, shared ones too.
3. Through an exclusive borrow, only its owned fields may be freed or reallocated, never
   the pointer itself, so the caller gets back what it lent.
4. Views need a write rule: a borrow may be written only into a view that is a named
   local.
5. Rule 7 can be weaker: ending a local's loans when its scope ends is enough.
6. Struct assignment needs its own rule: copying a struct with owned fields is rejected
   (it would be two owners), unless it is written as a move.

The list in `safe-c-lean/OWNERSHIP.md` ("Rules clang must enforce") supersedes the rules
above and is the implementation spec.

## Implementation, in two layers

1. **Type layer (Sema, per expression, cheap):** where each kind may be stored; moves;
   which operations need an owner or an exclusive borrow; `noescape` by default; plain
   pointer fields outside views. Errors say what to write (make the field
   `_Owned`, mark the struct `_View`, or use `_Unchecked` and
   `__unsafe` at the dereference).
2. **Flow layer (Clang's lifetime analysis):** loans from owned locals and owned field
   paths, ended by moves, frees, reallocs and owned-field writes; a use of a dead loan is
   an error. This extends upstream's loans and origins with field paths (upstream has
   field sensitivity in review), so the work stays upstreamable.

## What it costs (to be measured)

- cJSON: `next` and `child` become owned, `prev` (a back pointer) becomes unchecked,
  `valuestring` and `string` owned. The parser's cursor struct becomes a view.
- Idioms rejected, as in Rust: doubly linked lists without `__unsafe` on the back
  pointer, holding an element pointer across a `realloc`, storing a parameter in a
  struct without saying so.

## Order of work

1. Lean: extend the ownership model with unchecked pointers, views, exclusive borrows
   and the field rules above, and prove `own_sound` again (an agent, in the lean
   worktree).
2. Type layer in Sema, with a lit test per rule and a control run.
3. Flow layer in the lifetime analysis.
4. Audit gate: the 12 ownership and stack probes rejected; ports re-annotated and their
   cost recorded.
