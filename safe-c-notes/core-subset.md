# Core: a subset small enough to prove (proposal, 2026-11-24)

Goal: no use after free and no double free in code that compiles, proved in Lean for the
rules below and checked by one pass over ClangIR. Anything outside the rules is an error.
Nothing here is built or proved yet.

The main restriction is the one that removes lifetimes from types: **a borrow is never stored.**
(As in Hylo, and Rust without reference fields.)

1. **Owner:** `T *_Owned`. Using an owner by value moves it. A local is dead after; a field
   can only be moved with `_Take`, which leaves null.
2. **Borrow:** any other pointer to an object. It lives in a local or parameter only: not in a field,
   array element, global, or through a pointer (so no out parameters of borrow type).
3. **Shared or exclusive:** `const T *` is shared and const is deep (nothing mutable can be
   reached through it). `T *` is exclusive. One exclusive or many shared borrows of a place at a time,
   from creation to last use.
4. **Places:** local, `.field`, `*owner`, `*borrow`. `a[i]` counts as all of `a`.
5. **Calls:** an exclusive argument may not overlap any other argument. A returned borrow
   borrows from every pointer argument. No lifetime annotations.
6. **Function pointers** have the same types, matched exactly. No pointer casts, no `void *`,
   no untagged union of pointers, no setjmp, no atomics of pointers.
7. **Globals** hold no owners and no borrows except of constant static data.
8. **Any CIR operation not listed is an error.**

## Does real code fit? Pointer fields, classified by hand (struct, what it does)

| | owner | stored borrow | back pointer | static or function |
|---|---|---|---|---|
| parson (12) | 9 | 0 | 3 (`parent`, two `wrapping_value`) | 0 |
| cJSON, without the parse and print buffers (5) | 4, two sometimes not owned | 0 | 1 (`prev`) | 0 |
| redis adlist node and list (8) | 3 (`next`, `head`, `value`) | 0 | 2 (`prev`, `tail`) | 3 |

inih's reader, cJSON's parse buffer, zlib's `next_in`/`next_out` and adlist's iterator store a borrow in a struct
that never outlives a call or a local. Rule 2 rejects them; a second-class struct (local or parameter
only, like a borrow) is the likely extension. Back pointers become an index or an extra parameter.

## Order, riskiest first; stop at the first failure

1. Lean: rules 1 to 5 over a small place language, theorem: no use after free, no double free.
2. Write parson's value, array and object core in the subset by hand. Fails if it needs
   `__unsafe`, or an API change beyond dropping functions that return the parent.
3. The pass, with the 342 known-bad programs: any accepted one that goes wrong is a failure.
4. A blind attack on the pass.

## Step 2 result (2026-11-24): parson's core, refereed by rustc

`clang/utils/safe-c/core/parson_core.rs` is a hand transliteration of the object, array, dotted
path, deep copy and simplified parse functions, under the subset's limits. rustc accepts it and
its small test passes. Planting a use after remove in it is rejected. Not modeled: failed
allocation, serializing, schema validation, parson's own 362 checks. No C was written yet.

**By the bar above, the rules as written fail**: parson's API would have to change beyond the parent
functions. Two rules would keep it, and both are new and unproved:

9. **A result follows its argument.** 18 public functions take `const` and return a mutable
   `JSON_Value`, `JSON_Object` or `JSON_Array` pointer (3 of them are the parent functions).
   `T *f(const S *s)` returning something inside `*s`: the result is exclusive if the caller used
   an exclusive `s`, else shared. The body may only return what it reached through `s`. Rust
   writes each such function twice.
10. **Ownership on success.** 4 public functions take a value only if they succeed. The owner is
   "maybe moved" until the result is tested. Rust returns the value in the error.

Changes to code, not API: `const char **string` cursors become an index (18 lines mention
them), moving between arrays replaces copying pointers and rolling back, `memmove` of owners becomes a
loop of moves, and an undo path re-derives a borrow. 7 kinds in all, marked CHANGED in the file.

## The rules to prove (2026-11-24)

Heap: objects with owner fields (null or one object); a forest, each object owned by at most one owner local
or field. A local is an owner, or a borrow with a parent local and a mode.

- Borrow `x = v`, `x = v->f` through owner fields: x's parent is v. Creating it kills nothing. x is capable of
  changing structure if v is (owners are; parameters unless `const`) and x is not declared `const`.
- Mutate through v (store or take an owner field, free, move, argument declared without `const`): v must be live and
  capable; every live borrow with the same root except v and its ancestors dies. A dead local cannot be used.
- Reads and scalar writes only need a live pointer: they free nothing.
- `c = c->f`: the new c has the old c's parent, and so do borrows of the old c.
- Moving an owner (into a field, argument, return, local) also makes it dead. Overwriting a non-null
  owner leaks, which is allowed. free(o) frees one object; objects it owned leak.
- Call: arguments are owned (moved), taken on success, exclusive, shared. Arguments share a root only if
  all of those are `const` or point to types with no owners. The callee may only touch what it reaches from its parameters;
  an exclusive parameter's own object is not freed.
- Result: a fresh owner, or a borrow inside one named borrowed parameter (rule 9): in the
  caller a child of that argument, as capable as that argument.
- Taken on success (rule 10): the body must have moved the parameter on every path returning true and
  must still hold it live on every path returning false; in the caller it is maybe-moved, tied to the
  result, until a branch on that result. Its borrows die at the call.
- Branches join to the weaker state; loops need an invariant state. No path sensitivity otherwise.

Paths are left out on purpose: borrowing any part of v borrows all of v.
