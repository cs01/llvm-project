# The shapes that yield

A finding is worth one bug. The **shape** behind it is worth every instance of
that shape in every codebase, which is the difference between auditing and
tooling. This file is the catalogue: each shape, why the usual tools miss it, and
what it has found so far.

The loop that produced this file: `expat storeRawNames` was found by reading,
turned into a text-level scanner, and the scanner immediately found two more
instances in sqlite.

**That scanner has been removed, deliberately.** It found real defects and it
sidestepped the entire point: it matched source text and never once used a
contract, so nothing it reported was proved and nothing it reported exercised
the checker this project exists to build. The shapes below are kept because
they tell you *what to write a contract about*. Turning a shape into a
mechanical search is not the goal; turning it into a `pre` clause that a solver
discharges is.

The route for a new tree is [`verify-contract.sh`](verify-contract.sh):
annotate the function, then

```sh
./verify-contract.sh <function> <tu.c> -I <dir>
```

---

## Shape A — a pointer formed outside its object, never dereferenced

**The code computes an address it is not allowed to compute, uses it only in a
comparison or subtraction, and repairs it before any load.**

C defines pointer arithmetic only within an object and one past its end
(6.5.6p8), and relational comparison only between pointers into the same object
(6.5.9p6). Neither requires a dereference to be undefined.

*Why nothing catches it.* Nothing is loaded or stored, so ASan's instrumentation
never fires. On a flat address space the arithmetic produces the numerically
right answer and the program behaves, so no fuzzer input distinguishes it. It is
exactly the freedom a provenance-exploiting optimiser is permitted to take, which
is why it matters despite never crashing.

*How to find it.* `cbmc --pointer-overflow-check`. **`--pointer-check` alone
reports every instance below as clean** — that flag is the whole difference
between finding these and not.

*Found:* zstd `ZSTD_overlapCopy8`, zstd `BIT_initDStream`.
*Cleared:* zlib `inflate_fast`, which is the model of how to avoid it — compute
the distance as an **integer**, check it, and only then form the pointer.

*Detector:* none yet. The textual form is too weak; the property is semantic.
The tractable route is to annotate a function's documented contract and prove
it, which is what this branch is for.

---

## Shape B — a pointer read after `realloc` freed it

**`p = realloc(p_old, n)` moves the block, and the old pointer is compared or
subtracted before it is reassigned.**

C17 6.2.4p2: the old value becomes indeterminate the instant the block is
deallocated. Reading it at all is undefined, and the offset the code computes is
numerically correct on every real target, which is why it survives.

*Why nothing catches it.* Same reason as shape A, plus one more: whether
`realloc` moves the block is the allocator's choice, so no input reliably
reaches the bad case.

*Was mechanized as a text-level scanner, now removed; see the note at the top.*
Text-level, but no longer noisy: **4 hits across six trees, 4 real.** It started
at 1-in-6 and every false positive turned out to be a distinct, mechanizable
confusion rather than an inherent limit of grepping:

| What it confused | Example | Rule now applied |
|---|---|---|
| a sibling field for the pointer | `values.values_num` is not `values.values`; `.len` is not `.vec` | expand a stem to sibling members **only inside a `union`**, where the names really do denote one object |
| a write for a read | zlib's `buf = NULL;` | ignore the name when it is an assignment target |
| the failure path for the success path | redis `rdb.c` frees the old block inside `if (nv == NULL)` — correct, that is realloc's contract | compute the guard's block extent, and handle `== 0` as well as `== NULL`, `if(` without a space, and a body on the guard's own line |
| a size argument for the pointer | sqlite's `p->nAlloc = sqlite3DbMallocSize(db, zNew)` looks like a repair | a repair assigns the pointer itself, optionally cast — not a function *of* it |
| a substring for the name | `realptr` matched `ptr` | word boundaries on both sides |

Getting there needed the control both ways. Tightening once removed every false
positive **and the true positive in expat**, because its guard returns and
everything after it is the success path. A filter that silences the finding is
worse than the noise it removed, so each rule was checked against the known-real
sites before it was kept.

*Found:* expat `storeRawNames`, sqlite `fts3_unicode.c` and one more.
*Cleared:* zlib `pufftest.c`, zlib `enough.c`, redis `zmalloc.c` — all triaged
in [FINDINGS.md](FINDINGS.md).

---

## Shape C — the documented contract is weaker than the code needs

**A doc-comment states a precondition; the body requires something stronger; all
in-tree callers happen to satisfy the stronger one.**

Harmless today by construction — the callers are correct — and a trap for the
next caller, a rewrite, or anyone porting the function. This is the most common
finding by a wide margin, and the one this grammar is most directly for: writing
the documented contract down in a form a checker reads is what exposes the gap.

*How to find it.* Write the contract **as documented**, not as intended. Then
give the harness exactly what the documentation entitles a caller to provide,
and prove. Two runs, one variable:

| allocation | expected |
|---|---|
| exactly what the comment promises | fails, if the comment is wrong |
| what the code actually needs | clean |

*Found:* zlib `inflate_table` (comment says `2^bits`, body writes past it),
zstd `BIT_lookBits` (documents a bound 26 wider than its callee accepts), zstd
`ZSTD_execSequence` (preconditions live in asserts that `-DNDEBUG` deletes).

*Detector:* none, and probably none possible — it needs a human to read what the
comment claims. The leverage here is the grammar, not a scanner.

---

## Adding a shape

When a finding does not fit A, B or C, that is the interesting case. Write the
shape here **before** filing the finding, and answer three questions:

1. What is the rule being broken, with a citation?
2. Why does ASan, or a fuzzer, or the compiler, not already catch it?
3. Can it be detected mechanically? If yes, write the detector and run it over
   every tree in [FINDINGS.md](FINDINGS.md) before filing.

If the answer to 2 is "it would", it is not a shape worth cataloguing — the
existing tools are cheaper and someone else will find it first.
