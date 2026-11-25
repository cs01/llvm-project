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
