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

## Steps 2 and 3 in C (2026-11-24)

`core/check.ts` is a prototype checker over ClangIR that fails closed. `core/parson_core.c` (300 lines:
objects, arrays, dotted paths, remove, grow, deep copy, parse) passes it with no `__unsafe`, runs under
`-fsafe-c` and under ASan and UBSan, and 8 planted bugs are each rejected. 176 marks in 300 lines (92 `_Nullable`,
59 `_Owned`, 21 `__counted_by`, 4 `_Taken_if_true`). It is shaped like parson, not parson: counted keys, owner fields
for the union, no parent pointers. Workarounds: clang crashes on counted fields indexed through
`_Nullable` pointers, so null checks are split into helpers; locals of one name must be of one kind.

Corpus of the earlier attacks: 0 of the 351 programs ASan called use after free, double free or bad free were
accepted. That says little: only 8 of 459 ASan-clean corpus programs are accepted, since those use marks and
headers outside the subset. Two accepted programs fail in ASan for other reasons (b1r_bb37 SEGV, void_v08
stack overflow, which Safe C is recorded as trapping at run time; not rechecked here).

## Variety (2026-11-24; user: are we overindexing on parson?)

Functions in unmodified code that hit none of the bans a script can see (`core/variety.ts`, a crude proxy:
it counts every memcpy as a void pointer, and parson scores 43% though its core was written in the subset):
lua 58%, bearssl 54%, redis 47%, parson 43%, zlib 42%, cmark 41%, sds 40%, lz4 34%, libyaml 28%. Functions that
store a pointer in memory other than a fresh allocation or null: bearssl 2%, sds 5%, parson 10%, lz4 11%, lua 17%, redis 18%,
cmark 21%, zlib 22%, libyaml 34%. No Linux source is on this machine; nothing is known about it.

Added gate, before any verdict: slices of cmark's node tree, redis dict, zlib inflate and a Linux-style file.

Linux v6.12 (adc2186), part only, in ~/git/ref/linux-part: drivers/base, ipc, lib, drivers/misc/eeprom, include/linux.
Slices are chosen by rule, not by eye: in each directory, the non-test file of 150 to 500 lines whose path
has the lowest sha1. The Linux-style file in the gate is replaced by these four files, read and
classified, and the first one that has an intrusive list or a refcount is the slice.

## Escape hatch (user, 2026-11-24: "some unsafe or escape hatches is ok")

`_Trusted` on a function definition: its body is not checked, its signature is, and callers get the normal
rules. The claim becomes: no use after free or double free unless a trusted body breaks its signature's
promise. There is no hatch smaller than a function, so nothing unchecked flows into checked code except
through a signature. It is a count, not a free pass: each slice of the variety gate reports trusted
functions and lines. Fits = at most 5% of its lines trusted; fits with a type = trusted lines are only in
one reusable type under 100 lines; otherwise it does not fit.

## Variety gate: verdict (2026-11-24): at most 1 of 4, the bar was 2. The core is not a path to general C or Linux.

Judged by reading the code, not by writing the slices: three are ruled out by their interfaces, which
no amount of rewriting inside a function changes. Linux slice by the rule: the four files have no list or refcount, so
the next hash in drivers/base that has one, `firmware_loader/sysfs_upload.c`.

| slice | verdict | why |
|---|---|---|
| cmark node tree | does not fit | `cmark_node_unlink(node)` and `insert_before(node, sibling)` change the parent and siblings, which are not inside `node`. A `_Trusted` body cannot help: the signature would promise the opposite. Fits only as an index pool, with every signature changed. |
| redis dict | does not fit | keys and values are `void *`, 11 callbacks in `dictType`, and `dictTwoPhaseUnlinkFree(d, link, ...)` takes a container and a pointer into it. A trusted dict would be the whole 4,049-line file. |
| zlib inflate | not today | `next_in`/`next_out` are borrows in a struct; needs local-only structs (designed, not built). `strm` back pointer and three pointers into the struct's own array would become indices. Allocation callbacks. |
| Linux sysfs_upload.c | does not fit | `container_of`, a work item that outlives the call, owner and back pointer both ways (`fw_upload`, `fw_sysfs`, `fw_upload_priv`), `ops` callbacks, reference counts taken in one function and dropped in another, mixed owner and borrow fields. |

One missing idea accounts for most of it: an object changed through one pointer while others to it stay valid (shared
mutable). Rust handles that with reference counts, cells, locks, pinning and generics; the core has none.

What still stands: the core suits tree-shaped owners (parson's core, checked and run). Proof and blind
attack were still running when this was written; they decide only whether that narrow claim holds.

## Lean result (2026-11-24): proved, with one rule added

`SafeC/Core*.lean` (2,753 lines, `core_sound`, axioms propext, Classical.choice, Quot.sound; no sorry): a program the
modeled checker accepts never reaches use after free or double free, from an empty heap, with recursion, loops, rules 9
and 10. Added rule: a parameter is never assigned (two counterexamples proved without it). 17 necessity
programs. Not modeled: scalars, allocation failure, `_Trusted`, threads, and the C checker itself. "Moved on every path returning
true" only prevents leaks. This proves the model, not check.ts and not Clang; check.ts does not enforce the new rule except
for conditionally taken parameters, and was not compared with the model.

## Blind attack (2026-11-24): the narrow claim passes its bar

3,239 programs (1,170 hand-written batches, 1,369 templated, 700 random), 1,046 accepted. 2 root causes, 30 programs,
both in rule 10 (the scalar result of a conditional take): its address escapes and it is overwritten; it is narrowed so
nonzero becomes zero. 0.6 per 1,000 programs, 1.7 counting only hand-written ones; the bar was 5. Earlier ownership
checkers: 34 and 32 root causes in their two attacks. Both reproduced, fixed, with tests; 0 of the 30 are accepted now. No fresh attack since
the fixes. The checker had no `_Trusted` then. Not covered: -O2, large programs, threads, most multi-file. The attacker
is the same kind of model that wrote the checker, and was told, not forced, to stay blind.

Its 12 most common false rejections are the real cost: forwarding a take (`return put(p, x)`), `exit` not ending a path,
`?:` on owners, disjoint fields and elements, struct initializers, stdio, most of the library, callbacks, out parameters, `main(argc, argv)`.
