# A spatial annotation layer for C

This specifies a small annotation language for stating what a C function does to
memory. It is a layer *on top of* C, not a change to C. Annotated source is
still ordinary C: it compiles, unmodified, with any compiler, at any standard
level from C89 on, whether or not that compiler has ever heard of an annotation.

The scope is deliberately narrow: **spatial** properties — which buffers a
function may read, which it may write, how big they must be, and what it leaves
alone. Nothing about termination, ownership, or state over time. Spatial safety
is where the bugs are in C, and a language you can read in one sitting is worth
more than a complete one you cannot.

This document specifies the language. It never names a compiler or a checker;
see [Implementations](#implementations) at the end for what exists today.

## Contents

- [The disappearing act](#the-disappearing-act)
- [The shape of the language](#the-shape-of-the-language)
- [Roles](#roles)
- [Counts: bytes and elements](#counts-bytes-and-elements)
- [The result](#the-result)
- [The primitive layer](#the-primitive-layer)
- [Every element: `c_forall`](#every-element-c_forall)
- [Worked example](#worked-example)
- [What still needs a compiler change](#what-still-needs-a-compiler-change)
- [Out of scope](#out-of-scope)
- [What a conforming checker must do](#what-a-conforming-checker-must-do)
- [Implementations](#implementations)

## The disappearing act

Annotations are macros. Under a compiler that understands them they expand to
grammar that is parsed and type-checked with the function's parameters in scope.
Under every other compiler they expand to nothing at all.

```c
#include "c_contracts.h"

size_t decode(void *dst, size_t dstCap, const void *src, size_t srcSize)
  c_writes  (dst, dstCap)
  c_reads   (src, srcSize)
  c_returns (c_result <= c_old(dstCap));
```

Preprocessed by a compiler with no annotation support, that is exactly:

```c
size_t decode(void *dst, size_t dstCap, const void *src, size_t srcSize)
  ;
```

Three properties follow, and they are the reason for this shape:

- **No second code path.** Both expansions produce the same declaration, so
  there is no divergence between an annotated build and a shipping build to
  reason about. The usual objection to compiler-dependent macros is that they
  multiply the paths through a codebase; here the count stays at one.
- **No second translation stage.** Nothing is rewritten into another language
  before a compiler sees it. A checker reads the annotated source directly and
  proves its properties about the C abstract machine; code generation is
  whatever compiler the project already uses.
- **Not comments.** An annotation is a construct that can be checked. A comment
  is a construct nothing can check, and a comment that lies is worse than
  silence.

Every name in the language begins `c_`, which is the marker that says *this
vanishes*. Every macro takes a fixed argument count, so no variadic macros
appear and `-std=c89 -pedantic` stays quiet.

## The shape of the language

There are two layers, and almost all use is in the first.

A **role** says what the function does to a buffer. Roles are the spelling to
reach for, and they are the whole reason the language is short.

A **clause** is the primitive underneath: an obligation on the caller, a promise
by the callee, or a bound on what changed. Roles lower to clauses. You write a
clause directly only when no role can say what you mean — a global in the frame,
a partial write, a relation between two parameters.

The distinction that organises everything is *who owes it*:

| | who owes it | question it answers |
|---|---|---|
| a precondition | the **caller** | what must be true before the call? |
| a postcondition | the **callee** | what is true when it returns? |
| a frame | the **callee** | what could have changed? |

A *predicate* is an ordinary C expression over the function's parameters,
required to be free of side effects. It must not assign, must not use `++`, and
may call a function only if that function is marked pure. Every checker
evaluates a predicate more than once, so a predicate with a side effect means
different things to different checkers.

## Roles

Two of them.

### `c_reads(p, n)` — the function reads this buffer

The caller must supply `n` readable bytes at `p`, and `p` must not be null.
Because the function reads them, the caller must also have *initialized* them.

```c
unsigned hash(const void *src, size_t srcSize)
  c_reads (src, srcSize)
  c_writes_nothing;
```

### `c_writes(p, n)` — the function writes this buffer

The caller must supply `n` writable bytes at `p`, `p` must not be null, and —
this is the part that carries the weight — **nothing outside the listed buffers
changes**.

```c
void set_zero(void *dst, size_t n)
  c_writes (dst, n);
```

A single annotation states both halves because a function that writes a buffer
always means both: the caller has to own the memory, and the caller gets to keep
everything it knew about every other object. Other specification languages split
these into two clauses; the split is real underneath, and there is no reason to
make a reader perform it.

### Composing them

A function that reads a buffer and then writes it carries both roles:

```c
void scale(int *buf, size_t len)
  c_reads_n  (buf, len)
  c_writes_n (buf, len);
```

There is no third name for this, and the absence is deliberate. The difference
between `memset` and `buf[i] *= 2` is not intensity, it is whether the caller
must have initialized the memory — and that is exactly what the presence of
`c_reads` states. Handing freshly `malloc`'d storage to `scale` is a bug the
annotation can name; handing it to `set_zero` is fine.

### `c_writes_nothing`

Says the function modifies nothing a caller can observe. This is the strongest
frame claim available.

It is needed because **an annotation with no write role and no explicit frame
makes no claim about the frame at all**. A checker must then assume the function
may have written every object it can reach. So a pure reader says so out loud,
as `hash` does above.

## Counts: bytes and elements

One rule, in two places.

A role counts **bytes**, matching `memcpy` and every C interface that pairs a
`void *` with a size. The `_n` suffix counts **elements** of a typed pointer:

```c
c_reads    (src, srcSize)   /* srcSize bytes */
c_reads_n  (buf, len)       /* len ints, if buf is an int * */
```

`c_reads_n` exists because multiplying by `sizeof` in an annotation is noise the
checker can do for you, and because `c_reads(buf, len)` on an `int *` would
otherwise silently mean a quarter of the buffer.

Where a range appears in a clause, the second number is where it **stops**, not
the last element covered:

```c
c_assigns (c_range(buf, 0, 3))   /* buf[0], buf[1], buf[2]. Not buf[3]. */
```

Same convention as `for (i = 0; i < 3; i++)`, so the number you write is the one
already in your loop header.

## The result

```c
c_returns (c_result <= c_old(dstCap))
```

C has no way to *say* "the value this function returns" — `return` is a
statement, and the result has no name you can write in an expression. So the
language supplies one, always called `c_result`.

`c_old(e)` is the value `e` had on entry, and it is **required** rather than
optional when naming a parameter. In C every parameter is a by-value copy the
body may freely mutate; `src += 4` and `cap -= n` are ordinary in decoder code,
so a postcondition naming a bare parameter would be silently ambiguous between
its entry and exit value. This is the language's sharpest remaining edge — see
[below](#what-still-needs-a-compiler-change).

## The primitive layer

Five clauses, for what roles cannot reach.

| clause | says |
|---|---|
| `c_pre (P)` | P must hold when the function is called |
| `c_post (P)` | P holds when it returns |
| `c_assigns (L)` | the location L may be modified |
| `c_invariant (P)` | P holds on loop entry and after every iteration |
| `c_decreases (M)` | M strictly decreases and stays non-negative |

Clauses repeat; each is a separate obligation. `c_locations(a, b)` combines two
frame locations and nests when a single frame needs more.

The two cases that come up in practice:

```c
/* A global, which no role can name. */
void bump_counter(void)
  c_assigns (g_count);

/* Owning a whole buffer while writing only part of it. */
void set_header(void *buf, size_t n)
  c_pre     (c_writable(buf, n))
  c_assigns (c_range((char *)buf, 0, 8));
```

Inside a clause, three predicates describe memory directly. `c_readable(p, n)` and
`c_writable(p, n)` are the halves a role bundles. `c_fresh(p, n)` is stronger than
either: it says `p` points to a newly allocated object of *exactly* `n` bytes,
distinct from every other object under consideration, so an access past byte `n`
is detectable and non-aliasing is established.

```c
void zero(char *p, size_t n)
  c_pre     (n > 0 && n < 64)
  c_pre     (c_fresh(p, n))
  c_assigns (c_range(p, 0, n));
```

Bound the size before the `fresh` that uses it, so a checker constructing the
object is not asked for an unbounded one.

> A caution for readers arriving from other specification languages: some spell
> a `\fresh` that means a *post-state* property, "newly allocated during this
> call". This is not that. Here it describes the storage the caller hands over.

`c_invariant` and `c_decreases` sit between a loop header and its body, and are
listed for completeness — they are temporal rather than spatial and are not
specified by this document.

## Every element: `c_forall`

Every construct so far names a *place* — a scalar, a member, a contiguous
buffer. Some facts are about every element of a collection instead:

```c
int all_zero(const char *p, size_t n)
  c_reads (p, n)
  c_pre   (c_forall(i, 0, n, p[i] == 0));
```

Read it as: for every `i` starting at 0 and stopping before `n`, `p[i]` is zero.
Same half-open convention as a range. The bound variable is an unsigned integer,
in scope only for the predicate and not for the bounds, so `c_forall(i, 0, i, P)`
is an error rather than a self-reference.

`c_forall` is an expression, so it works in any clause and may call the memory
predicates. This states something no C source could otherwise say — that every
live slot of a ring buffer holds a readable entry:

```c
struct entry *ring_get(struct ring *r, size_t idx)
  c_pre (idx < r->len)
  c_pre (c_forall(i, 0, r->len,
           c_readable(r->buffer[(r->first + i) & r->mask], sizeof(struct entry))));
```

Bounds must be unsigned, or non-negative constants. A signed variable is
rejected: converting a negative bound to an unsigned index would silently make
the range empty or enormous, and a vacuously true annotation is worse than none.

`c_forall` is the one place this language stops being guessable. It is here
because the alternative — saying nothing about arrays of pointers — gives up
most of what is interesting about a C buffer.

## Worked example

```c
size_t ZSTD_decompress(void *dst, size_t dstCap,
                       const void *src, size_t srcSize)
  c_writes  (dst, dstCap)
  c_reads   (src, srcSize)
  c_returns (c_result <= c_old(dstCap) || ZSTD_isError(c_result));
```

Three lines a caller can rely on without reading a line of the body: hand over
`srcSize` initialized bytes and `dstCap` writable ones, neither pointer null;
nothing outside `dst` changes; the result is either an error code or a length
that fits.

And that separation is the point. Checking `ZSTD_decompress` against its own
annotation happens once. Checking a *caller* then uses only the annotation and
never opens the body — so two hundred call sites cost two hundred cheap checks
against three lines, not two hundred re-analyses, and a caller can be checked
when all you have is the header.

## What still needs a compiler change

Two things in the language above are worse than they should be, and neither can
be fixed by the macro layer. Recorded here so they read as known debt.

**`c_old` is mandatory.** `c_returns (c_result <= c_old(dstCap))` should be
`c_returns (c_result <= dstCap)`, with the entry value as the default and an
explicit marker for the rare case that wants the exit value of a by-value copy.
A macro receives the predicate as opaque tokens and cannot wrap parameter
references, so this has to move into the front end.

**Roles sit after the parameter list, not on the parameter.** The buffer and its
length are already adjacent in the signature, and the annotation would read
better there:

```c
size_t ZSTD_decompress(c_writes(dstCap) void *dst, size_t dstCap,
                       c_reads(srcSize)  const void *src, size_t srcSize);
```

A macro cannot move a clause from a parameter to the declarator suffix, so this
also needs front-end support. The precedent is good: this is the shape of
Microsoft's SAL, which annotated an entire operating system API as macros that
vanish for other compilers, and of `__counted_by`, which already solves the
awkward part — resolving a length parameter named before it is declared.

## Out of scope

Named here so their absence reads as a decision rather than an oversight.

- **Termination and loop reasoning.** `c_invariant` and `c_decreases` exist and
  are spelled above, but they are temporal, not spatial.
- **Ownership.** Who frees what, and when.
- **Type invariants.** Properties of a `struct` that hold between calls.
- **Annotations on function pointers.**
- **Behavioural case analysis.** A postcondition already expresses cases with
  the operators C has: `c_returns (is_error(c_result) || c_result <= c_old(cap))`.

## What a conforming checker must do

Nothing here requires a verifier. A checker may implement any subset, and the
levels are worth separating because they cost very different amounts:

1. **Well-formedness.** The annotation is parsed with the function's parameters
   in scope and type-checked. Reject a predicate with side effects, a
   postcondition naming a bare parameter, a signed `c_forall` bound, a
   redeclaration whose annotation differs. Costs nothing; catches annotations
   that cannot mean what they appear to.
2. **Call sites.** Report a call that provably breaks a precondition. This is
   necessarily incomplete — the absence of a report is not evidence of absence —
   and it must never report a call it cannot prove wrong.
3. **Bodies, exhaustively.** Prove the function honours its own annotation for
   every input. This is what makes caller-side checking sound, and it is the
   only level that catches a frame that omits something.

A checker must not silently skip an annotation it cannot handle. Saying nothing
looks identical to a passing check, and a false sense of coverage is the one
failure mode an annotation language cannot tolerate. Decline out loud.

No level of checking makes an annotation *right*. Every one of them proves the
code matches what you wrote; none proves you wrote the correct thing.

## Implementations

`c_contracts.h` is the language. The grammar it expands to is a target chosen at
include time, and there are three:

| target | selected by | what you get |
|---|---|---|
| a contract-aware front end | `__has_feature(c_contracts)` | levels 1 and 2 as compiler diagnostics, and lowering to a verifier for level 3 |
| CBMC's own front end | `-DC_CONTRACTS_CPROVER` | level 3, with no contract-aware compiler in the pipeline |
| everything else | default | the annotations vanish; the code builds |

The third column is the whole argument for putting the language in a header
rather than in a compiler. The same annotated source reaches a verifier by two
independent routes, and builds everywhere by a third.

The role layer is header-only — nothing about it is approximate.
`c_writes (dst, dstCap)` lowers to

```
__CPROVER_requires((dst) != 0)
__CPROVER_requires(__CPROVER_w_ok((dst), (dstCap)))
__CPROVER_assigns(__CPROVER_object_upto(((char *)(dst)), (dstCap)))
```

which is exactly what the three separate clauses produce.

### Measured

A loop-free function, annotated once, taken down all three paths:

```c
int get(int *buf, unsigned len, unsigned i)
  c_reads_n (buf, len)
  c_pre     (i < len)
  c_writes_nothing
{ return buf[i]; }
```

- **contract-aware front end** — levels 1 and 2 clean
- **`gcc -std=c89 -pedantic -Wall`, `g++`, stock clang** — clean, annotations gone
- **`goto-cc -DC_CONTRACTS_CPROVER` → `goto-instrument --enforce-contract get` →
  `cbmc --function get --pointer-check --bounds-check`** — VERIFICATION
  SUCCESSFUL

Weaken the precondition to `i <= len` and the third path returns VERIFICATION
FAILED, *pointer outside object bounds in `buf[i]`*. The bound is meaningful
because `c_reads_n` supplied the extent; without the role there is nothing for
the checker to exceed.

### One caveat on spellings

A contract-aware front end accepts bare intrinsics such as `readable`, `old`,
`result`, `same_object`, and `pointer_offset`, plus a `p[lo : hi]` range and
`forall (i : lo, hi) P`. Those are not portable macros. Source that also targets
CBMC directly uses `c_readable`, `c_old`, `c_result`, `c_same_object`,
`c_pointer_offset`, `c_range(p, lo, hi)`, `c_locations(a, b)`, and
`c_forall(i, lo, hi, P)`.

### Loops on the CBMC-direct path

A function with a loop needs `c_invariant` and `c_decreases` before its own
contract can be enforced, because `--enforce-contract` requires a loop-free body
after loop contracts are applied. Given those it works — but the two transforms
must be **two separate invocations**. Combining them in one fails with *Loops
remain in function 'f', assigns clause checking instrumentation cannot be
applied*, even when the loop is fully annotated:

```sh
goto-cc -DC_CONTRACTS_CPROVER -o f.goto f.c
goto-instrument --apply-loop-contracts f.goto f-a.goto
goto-instrument --enforce-contract fill  f-a.goto f-b.goto
cbmc --function fill --pointer-check --bounds-check f-b.goto
```

On `fill(int *buf, unsigned len)` carrying `c_writes_n (buf, len)`, and a loop
carrying `c_assigns (c_locations(i, c_range(buf, 0, len)))`, `c_invariant` and
`c_decreases`, that reports VERIFICATION SUCCESSFUL — for every `len`, by
induction rather than by unrolling. Change the loop bound to `i <= len` and
three checks fail independently: invariant preservation, the decreases clause,
and *`buf[i]` is assignable*.

A multi-location loop frame is written with `c_locations`, which defers the
comma past macro-argument parsing so a one-argument `c_assigns` can carry a
list. CBMC takes one `__CPROVER_assigns` per loop rather than several, so on
this path the spelling is not optional.

For the front end's syntax, flags and internals, see
[contracts-reference.md](contracts-reference.md). For the design rationale and
the rejected alternatives, see [contracts-design.md](../contracts-design.md).
