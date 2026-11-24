# Tagged unions in safe-c-lean (M4 design proof)

A Lean model of M4 tagged unions (`safe-c-notes/m4-tagged-unions.md`):

```c
struct shape { int kind; union _Tagged_by(kind) {
  double radius _When(0); struct { int w, h; } rect _When(1); int *p _When(2); }; };
```

Structs with a tag and a union, tag writes, member writes and reads, tag
tests and `switch`, initializers, pointers to structs with writes through
them, and calls that may rewrite any struct whose address has escaped. It has
a soundness theorem for the full rule set and, for every rule, a program that
passes the checker without that rule and reads a member as another type.

```
lake build      # green, no sorry, no warnings
```

## Files

- `SafeC/Tag.lean`: syntax, the big-step semantics, the `Rules` switches and
  the checker (`chk`, `check`).
- `SafeC/TagSound.lean`: the invariant, the proof, the main theorem, and
  `#print axioms tag_sound`.
- `SafeC/TagHoles.lean`: one counterexample per rule and the accepted idioms,
  all checked by `decide` in the kernel; `idioms_sound` applies the theorem to
  the idioms for all fuel.

## The theorem

```lean
theorem tag_sound (L : Lay) (G : List Nat) (s : Stmt) (A : Abs)
    (hc : check Rules.full L G s = some A) (fuel : Nat) :
    (exec L fuel (St.init G) s).isUB = false
```

For any layout `L` and globals `G`, if the checker accepts `s` under the full
rules, running it from the zero state never reaches UB, for any fuel. The only
UB is `typeConfusion`: reading member `m` while the union holds the bytes of a
member `m'` of a different type. What is left is `ok`, `trap` (a write or read
through a null pointer) or `outOfFuel`. `#print axioms` lists only `propext`,
`Classical.choice` and `Quot.sound`.

### Semantics

A struct's storage is its tag, the member whose bytes the union holds (a ghost
record of who wrote them) and the payload. A layout gives each member a type
and maps each tag value to the member it makes live; because that map is a
function, a value names at most one member. Every struct starts as `{0}`.
Globals and structs whose address is taken are escaped; a call performs a
list of whole-struct writes, each only if its target has escaped, since a
callee cannot name anything else.

### Proof idea

The checker state gives, per struct, the list of tag values it may hold on
every path here and a written flag. `Inv` says: the concrete tag is in the
list; if the flag is set, the stored member is the one the tag makes live;
escaped structs and pointer targets are in the static escape set `E`. A read
of `m` is accepted only when every listed tag makes `m` live and the flag is
set, so the stored member and `m` are live for the same tag and are the same
member. An aliasing write extends the list with every tag value that names a
member and keeps the flag: a whole-struct write is consistent, and no write
leaves the struct as it was.

## Rules, and what each counterexample shows

Each rule is a field of `Rules`; `Rules.full` has them all. For each,
`TagHoles.lean` gives a program that passes the checker with only that rule
off, then runs it to `typeConfusion`. With the rule on, the same program is
rejected. Layout `struct T { k; union { long i _When(I); int *p _When(P); } }`
from `safe-c-tagged-holes.c`.

| Rule | Counterexample | Program |
|---|---|---|
| `readNeedsTag` | `need_readNeedsTag` | `t = {.k = I, .i = X}; y = t.p;` |
| `writeNeedsTag` | `need_writeNeedsTag` | `t = {.k = P, .p = &g}; t.i = X; y = t.p;` |
| `tagWriteUnwrites` | `need_tagWriteUnwrites` | hole 06: `t.i = X; t.k = P; y = t.p;` |
| `testKeepsUnwritten` | `need_testKeepsUnwritten` | `t = {.k = I, .i = X}; t.k = P; if (t.k == P) y = t.p;` |
| `aliasKills` (pointer) | `need_aliasKills_ptr` | `q = &t; *q = (struct T){.k = I, .i = X}; y = t.p;` |
| `aliasKills` (call) | `need_aliasKills_call` | hole 14: `if (TG.k == P) { rt(); y = TG.p; }`, `rt` sets `TG` to `I` |
| `untrackedRejected` | `need_untrackedRejected` | hole 07: a read through a place whose tag is not followed (`q->p`, `a[j].p`) |
| `initMatch` | `need_initMatch` | hole 12: `t = {.k = P, .i = X}; if (t.k == P) y = t.p;` |
| `initMatch` (callee) | `need_initMatch_call` | the callee writes `TG = {.k = P, .i = X}` |

`testKeepsUnwritten` is not in the compiler's list. It is the rule that a tag
test narrows the tag but must not mark the union written. The compiler built
on 2026-11-24 (`llvm-safe-c/build.noindex`) accepts this:

```c
struct T t = {.k = I, .i = 0x41414141};
t.k = P;
if (t.k == P) return *t.p;   // accepted; reads the long's bytes as a pointer
```

`edgeFacts` calls `setFact(F, P, V)` with the default `Written = true`, which
replaces the unwritten fact the tag write left. The same happens after an
aliasing kill: `killRoot` and the `erase_if` calls drop the fact, and a later
test re-creates it as written. The fix the model proves: a test keeps the
written flag of the fact it narrows, and a kill keeps the flag (an absent
fact must mean "tag unknown, written unknown", so the test needs the prior
flag, not a default of true).

## Idioms under the full rules

- `switch_ok`: a switch over every shape variant with the last as `default`;
  the default knows the tag from the cases it did not take.
- `switch_after_call_ok`: the same after a call that may rewrite the shape.
- `switch_miss_rejected`: a missing case, then reading in `default`, rejected.
- `tag_then_member_ok`: tag write, member write, read (h06_ok).
- `result_ok`: `SAFEC_RESULT`-style `bool ok` tag across a call that may
  rewrite it; the `else` arm knows `ok` is false.
- `local_survives_ok`: a struct whose address is never taken keeps its tag
  across a call and a pointer write to another struct.

## What the model does not cover

- **No pointer narrowing.** The compiler narrows `e->k == P` for a pointer
  local `e`; the model rejects every member read through a pointer (that is
  `untrackedRejected` applied to all of them). Writes through a pointer are
  whole-struct writes only.
- **Escape is flow-insensitive.** A struct whose address is taken anywhere is
  escaped everywhere; the compiler's `mayBeAliased` is similar.
- **Calls** are a list of whole-struct writes to escaped structs, checked by
  `initMatch` as the callee would be. A callee's tag write without a member
  write is not modeled.
- **Exhaustive switch** (design rule 4) is not a memory-safety rule and is not
  modeled; a switch is the chain of tests it compiles to, without fallthrough.
- **One struct type** per program, one union per struct, one payload cell per
  member; members of the same type may alias without UB (as `int` punning of
  the same type does in C).
- **No loops**, no scalar arithmetic, no tag values computed at run time.
- **Owned members** (design rule 3 for M2) and borrows of members (rule 5,
  `&s.c` ended by a tag write) are not modeled; they are M2 loans.
- **Initial state**: every struct is `{0}`, counted as unwritten, so a member
  is readable only after an initializer or a member write.
