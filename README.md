# Contracts for C in Clang

This branch makes Contracts a first-class feature of clang for C.

* A new Contract syntax for C
* Expressiveness and ergonomics of contracts and Clang warnings
* Guarantees of a battle-tested formal verification tool, CBMC, to mathematically guarantee your code matches your contract

## What are Contracts?

Contracts let you annotate your code with attributes or properties you want to
guarantee that it holds. For example, you can guarantee that:

* a function accepts only values within a certain range (`pre`)
* a function returns only values within a certain range (`post`)
* a function modifies nothing except a specific buffer you expect to be modified (`assigns`)
* a while loop always terminates (`decreases`)

A formal verifier can *mathematically prove* the code holds these properties for
every possible input.

## Simple Example

Let's say you have a memory allocator that accepts bytes to allocate.
Generally, one might put an `assert(n > 0)` at the top of the
function since we'd always expect a positive number of bytes to allocate.

This requires a.) someone to write the assert, and b.) someone to run the code to find the error.

Instead, we can define a *precondition*:

```c
#define C_CONTRACTS_NO_PREFIX
#include <c_contracts.h>

int *allocate(unsigned long n)
  pre (n > 0);
```

Now if you call `allocate(0)`, you will get a compile warning about the contract violation:

```
demo.c:6:12: warning: precondition n > 0 of 'allocate' is violated by this call [-Wcontract-violation]
    6 |   int *p = allocate(0);
      |            ^~~~~~~~~~~
demo.c:2:3: note: precondition declared here
    2 |   pre (n > 0)
      |   ^~~~~~~~~~~~
```

Note that we didn't need to add an assertion, and we got the warning at compile-time, not runtime.

## Why would you want to use Contracts?

In short: Contracts let you write and guarantee code correctness and absence of bugs.

Contracts are used for Formal Verification, to mathematically prove properties
of a program.

Formal Verification has traditionally been somewhat niche since it takes more effort to
annotate and run provers on code. In the past, only safety- and
security-critical code would even consider doing formal verification. For example, medical, aerospace,
nuclear, etc. applications.

But now, with AI, it is not nearly as difficult to generate tedious code like contracts.

And with the sheer volume and speed of AI code generation, code correctness is more
difficult to prove than ever. Thus, to many developers, having
strong guarantees that contracts provide are more useful than ever.

Unit tests work for the cases you test. Contracts and formal verification work for *all possible inputs* and provide stronger guarantees than unit tests.

## How does it work?

1. Clang parses the new contract annotations as first-class citizens. The Contract is part of the AST.
2. Clang verifies the syntax and semantics of the contract. Clang will warn you of any obvious contract violations such as passing literals that violate the contract.
3. Clang transpiles the code and its contracts to C Bounded Model Checker ([CBMC](https://github.com/diffblue/cbmc)), a formal verifier for C
4. CBMC formally verifies the code with your contracts. This is a deep verification, over every possible input rather than the ones you thought to test.

CBMC is used in several large scale applications, including
* AWS, on s2n-tls and aws-c-common
* FreeRTOS's TCP/IP stack
* Kani, the Rust verifier, which is built on top of CBMC

Three checkers read the same contract, each catching what the one before it let
through: the **front end** (is the contract well formed?), the **call-site
pass** (does any caller break it?), and **CBMC** (is it true for every input?).
All three work today; the walkthrough with real diagnostics is in
[the reference](docs/contracts-reference.md#the-three-levels-in-detail).

| The bug | Level 1<br>front end | Level 2<br>call-site | Level 3<br>CBMC |
|---|:---:|:---:|:---:|
| **Contract:** `post` names a mutated parameter without `old()` | **caught** | *n/a* | *n/a* |
| **Contract:** predicate calls an impure function | **caught** | *n/a* | *n/a* |
| **Code:** `put(b, 8, 8, 1)` — `len` and `i` are both 8, breaking `i < len` | missed | **caught** | caught |
| **Code:** `put(b, n, k, 1)` — the same bug, but `len` and `i` are variables | missed | missed | **caught** |
| **Code:** off-by-one in the callee's own loop | missed | missed | **caught** |
| **Code:** violation only on a loop's second iteration | missed | missed | **caught** |
| **Contract: well formed, and says the wrong thing** | missed | missed | **missed** |


## Installation
Install dependencies, [CBMC](https://github.com/diffblue/cbmc) 6.x, with
`goto-cc` and `goto-instrument`. Ubuntu ships 5.95, whose loop-contract handling
differs, so take a release `.deb` from the CBMC repository instead.

Build clang from this branch:

```sh
cmake -G Ninja -S llvm -B build \
  -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=host -DLLVM_ENABLE_ASSERTIONS=ON \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DLLVM_OPTIMIZED_TABLEGEN=ON
ninja -C build clang
```

`host` rather than a named target: with only `X86` on an arm64 machine the
build succeeds and then cannot produce a native binary, so
`-fcontract-runtime-checks` has nothing to run. Add `-DLLVM_USE_LINKER=lld` if
you have lld; it is a large link.

## Usage

| Flag | What it does |
|---|---|
| `-fc-contracts` | enable the keywords. Without it they are ordinary identifiers |
| `-Wcontract-violation` | warn at call sites that provably break a precondition (on by default) |
| `-fcontract-runtime-checks` | check each precondition at function entry at run time |
| `-fcontract-emit-cprover` | print each function's contracts as CBMC clauses |
| `-fcontract-emit-cprover-unit` | rewrite the whole translation unit into CBMC form, ready for `goto-cc` |
| `-fcontract-emit-harness` | emit a CBMC entry point per contracted function, built from its preconditions |
| `-Wc-contracts` | advisories about clauses that will not buy what they look like they buy (on by default) |
| `-Wcontract-runtime-coverage` | list the clauses `-fcontract-runtime-checks` cannot check (off by default) |

`-Wc-contracts` is the one that matters when annotating existing code. It is
what tells you, on real source, that a clause is not going to be checked:

```
warning: contract on 'ZSTD_wildcopy' has no effect on callers: the function is
         'always_inline', and a loop or function contract does not transfer to
         the inlined copy
warning: 'ZSTD_execSequence' has a contract but no 'assigns' clause, so its
         frame is empty; every write visible outside the function will be
         reported as a violation
warning: 'ZSTD_execSequence' has a contract but its body contains a loop with
         no loop contract; contract checking needs a loop-free body and will be
         refused
```

Each names the function, and the last points at the loop that needs the
annotation. Without these a contract can look adopted while checking nothing.

### Annotations that compile everywhere

Contracts are real grammar, which would normally mean annotated source only
builds with this compiler. `clang/lib/Headers/c_contracts.h` removes that
constraint, and does one more thing: it is where the language you actually write
lives. The keywords above are what it lowers to.

```c
#define C_CONTRACTS_NO_PREFIX
#include "c_contracts.h"

size_t decode(void *dst, size_t dstCap, const void *src, size_t srcSize)
  writes  (dst, dstCap)
  reads   (src, srcSize)
  returns (c_result <= old(dstCap));
```

`writes` is one annotation covering three clauses — `dst` is non-null, the
caller must supply that much writable memory, and nothing outside it changes.
The `writable`-versus-`assigns` distinction is exact underneath and absent from
the surface. There is no `c_updates`: a function that reads a buffer then writes
it carries both roles, and the presence of `reads` is what says the caller
must have initialized the memory.

GCC, MSVC, tcc and stock clang preprocess the whole thing to
`size_t decode(...) ;` — the same declaration the annotated build produces, so
there is no second code path. Verified against `gcc -std=c89 -pedantic -Wall
-Wextra`, `g++`, and stock clang. All three checking levels work through the
macros, and a diagnostic adds a `note: expanded from macro` pointing back at the
definition. Whole-translation-unit CBMC emission preprocesses the source first,
as `proofs/verify-contract.sh` does, so the rewriter sees the expanded clauses.

| write this | it means |
|---|---|
| `reads (p, n)` | non-null, `n` readable bytes, initialized by the caller |
| `writes (p, n)` | non-null, `n` writable bytes, and nothing else changes |
| `reads_n` / `writes_n` | the same, counting elements of a typed pointer |
| `c_writes_nothing` | modifies nothing a caller can observe |
| `returns (P)` | P holds of `c_result` when it returns |
| `pre` `post` `assigns` `loop_invariant` `decreases` | the clauses, for what a role cannot say |
| `range` / `locations` | one range, or several locations in a frame |

The header is self-contained C89 with no includes: vendor it into a project
rather than depending on a compiler to ship it. `__has_feature(c_contracts)` is
what it tests, and is available directly if you want to gate on it yourself.

#### Two spellings, and which to write

Every name has a `c_`-prefixed form. `#define C_CONTRACTS_NO_PREFIX` before the
include drops the prefix from all of them — the clauses `pre`, `post`,
`assigns`, `loop_invariant`, `decreases`, `old`, the roles `reads`, `writes`,
`reads_n`, `writes_n`, `returns`, and the predicates `readable`, `writable`,
`fresh`, `same_object`, `pointer_offset`, `range`, `locations`. That is the
spelling this README uses and the one to write: under `-fc-contracts` the
unprefixed clauses are the actual grammar, so the macros get out of the way
entirely.

Four names keep the prefix under every setting, because they are object-like
macros rather than function-like ones and would otherwise rewrite every bare
occurrence of a common word: `c_result`, `c_ssize_t`, `c_ghost`, and
`c_writes_nothing`. `c_forall` keeps it too, since the keyword binds its
variable with syntax the portable four-argument form cannot reproduce.

Before opting in, check the project for collisions the way the header's own
comment says: grep for `name(` rather than the bare word. A function-like macro
expands only where the name is followed by `(`, so `int pre;`, `s.post` and a
struct field called `range` are all unaffected.

#### `c_ghost`: a value that exists only for the contract

A loop contract usually has to name where the loop started, and C has no word
for that. `c_ghost` marks a declaration as belonging to the specification: it is
in scope for the clauses, and it compiles away with them.

```c
{   BYTE* const opStart c_ghost = op;
    while (op < oend)
      loop_invariant (same_object(op, opStart))
      loop_invariant (pointer_offset(op) >= pointer_offset(opStart))
      decreases (pointer_offset(opStart) + (c_ssize_t)length - pointer_offset(op))
    { *op++ = *ip++; }
}
```

That is real zstd, from `ZSTD_safecopy`. Without `opStart` there is nothing to
write the invariant against, and without the invariant the loop has to be
unwound to a fixed bound instead of proved.

### Verification without this compiler

The grammar above is one of three targets the header can expand to.
`-DC_CONTRACTS_CPROVER` selects CBMC's own, so the same annotated source reaches
a verifier with no contract-aware compiler in the pipeline:

```sh
goto-cc -DC_CONTRACTS_CPROVER -o get.goto get.c
goto-instrument --enforce-contract get get.goto get-chk.goto
cbmc --function get --pointer-check --bounds-check get-chk.goto
```

On `int get(int *buf, unsigned len, unsigned i)` carrying
`reads_n (buf, len)` and `pre (i < len)`, that reports VERIFICATION
SUCCESSFUL; weaken the precondition to `i <= len` and it reports VERIFICATION
FAILED, *pointer outside object bounds in `buf[i]`*. The bound is meaningful
because the role supplied the extent.

The unprefixed spelling works here too, so one annotated source reaches all
three targets unchanged: verified by compiling the example above with this fork,
with stock `cc -std=c89 -pedantic`, and through `goto-cc -DC_CONTRACTS_CPROVER`.
`c_result`, `c_ssize_t`, `c_ghost`, `c_writes_nothing` and `c_forall` keep their
prefix on every target, as above. Details and limits are in
[docs/annotation-spec.md](docs/annotation-spec.md#implementations).

For the annotation language on its own terms — no compiler, no verifier, spatial
properties only — see **[docs/annotation-spec.md](docs/annotation-spec.md)**.

### What `-fcontract-runtime-checks` covers

It compiles a precondition into a branch at the callee's entry that calls
`__contract_violation()`. That symbol is weak and traps by default, so a program
with its own fault handler can define it and win. Its declaration is:

```c
void __contract_violation(const char *predicate, const char *file,
                          unsigned line, const char *function);
```

A replacement may return after recording the violation; execution then
continues into the function. A handler that wants enforcing behavior should
terminate, trap, or otherwise not return.

The flag is opt-in, and covers less of a contract than its name suggests:

| Clause | At run time |
|---|---|
| `pre (n > 0)`, and anything else over scalars | checked |
| `returns (...)` | not checked; compile-time and CBMC only |
| `pre (readable(p, n))`, `writable` | declined: an allocation's bounds cannot be recovered from a `void *` |
| `pre (c_forall(...))` | declined: the range is not known until the call |
| `assigns` | not checkable without shadow memory |

A declined precondition produces a warning naming the reason, because a check
that silently passed would look like coverage while providing none.

The optional `-Wcontract-runtime-coverage` diagnostic lists proof-only clause
kinds such as `post`, `assigns`, `loop_invariant`, and `decreases`. It is off by
default so enabling runtime checks on an annotated library does not produce an
unfixable warning for every deliberate proof-only clause.

To run some examples, build clang, then try:

```sh
./contracts-example/run.sh          # or CLANG=/path/to/bin/clang ./contracts-example/run.sh
```

## Writing contracts

A clause holds a *predicate*: an ordinary C expression, with the function's
parameters in scope, that has to be free of side effects. A call inside one is
allowed only if the callee is marked `const` or `pure`; the compiler rejects the
rest.

### `pre`: what the caller must guarantee

A `pre` is a condition the caller has to satisfy before the call:

```c
int *allocate(unsigned long n)
  pre (n > 0);
```

`allocate(0)` is now a mistake the compiler can name, rather than whatever the
body happens to do with it.

### `returns` and `post`: what the function guarantees

`returns` states what holds of `c_result` when the function returns. Use
`post` for a postcondition that does not mention the result:

```c
int *allocate(unsigned long n)
  pre (n > 0)
  returns (c_result != 0);
```

A caller that checks the result for null is now re-checking something the callee
already promised, and one that skips the check is relying on the contract rather
than guessing.

`old(e)` is the value `e` had when the function was entered, and is legal only
inside a postcondition. You need it because a function is free to modify its own
parameter copies, so by the time it returns, a bare `n` may no longer be the
`n` the caller passed:

```c
size_t drain(char *buf, size_t n)
  returns (c_result <= old(n));
```

### `readable`: a buffer the function may read

Almost every interesting, and dangerous, C function takes a pointer and a
length. `readable(p, n)` says the caller must hand over `n` bytes at `p` that
are safe to read:

```c
unsigned hash(const void *src, size_t srcSize)
  pre (readable(src, srcSize));
```

Pass a shorter buffer than you claimed and the mistake is in the contract, at
the call, instead of in whatever `hash` reads off the end.

### `writable`: a buffer the function may write

`writable(p, n)` is the same promise for writing, `n` bytes at `p` the function
is allowed to store into:

```c
size_t decode(void *dst, size_t dstSize, const void *src, size_t srcSize)
  pre (readable(src, srcSize))
  pre (writable(dst, dstSize));
```

It says the write would be *legal*, not that it happens. What actually changes
is `assigns`, below.

### `fresh`: an exact, distinct object for a proof

`fresh(p, n)` says `p` points to a newly allocated object of exactly `n` bytes,
distinct from every other object in the proof. This is stronger than
`readable(p, n)` or `writable(p, n)`: those predicates establish access to at
least `n` bytes, while `fresh` gives the object an exact boundary, allowing the
prover to detect an access past byte `n` and reason about aliasing.

It also tells the generated CBMC harness what storage to create:

```c
void zero(char *p, size_t n)
  pre (n > 0 && n < 64)
  pre (fresh(p, n))
  assigns (range(p, 0, n));
```

The harness assumes the size bound first, allocates `n` bytes for `p`, and then
calls `zero`. Clause order matters: put bounds on `n` before `fresh(p, n)` so
the harness does not attempt an unbounded allocation.

Repeating the same `fresh` constraint does not allocate the target twice. If
two `fresh` constraints give the same target different sizes, harness emission
stops with an error instead of generating an infeasible proof.

`fresh` belongs to the proof tier. Ordinary C cannot recover an allocation's
extent or uniqueness from a pointer at function entry, so runtime checking
diagnoses the clause instead of pretending to enforce it.

### `c_forall`: every element of a range

The clauses so far can name a scalar, a member, or a slice. None of them can say
something about *every* element of a buffer, which is most of what you want to
know about one:

```c
int all_zero(const char *p, size_t n)
  pre (readable(p, n))
  pre (c_forall(i, 0, n, p[i] == 0));
```

Read the second clause as: for every `i` starting at 0 and stopping before `n`,
`p[i]` is zero. The upper bound is where it stops, not the last value it takes,
so the number you write is the one already in your loop header. `i` takes its
type from the bounds.

The bound variable has type `size_t`. `c_forall` goes inside a predicate, so it
works in a `pre` or a `post`, and it can
call the buffer predicates. This says every live slot of a ring buffer holds a
readable entry, which nothing in the C source could otherwise state:

```c
struct entry *ring_get(struct ring *r, size_t idx)
  pre (idx < r->len)
  pre (c_forall(i, 0, r->len,
         readable(r->buffer[(r->first + i) & r->mask], sizeof(struct entry))));
```

The `c_forall` macro expands to the compiler's contextual `forall` expression.
Bounds must be unsigned or non-negative integer constants. A signed variable is
rejected because C would convert a negative value to `size_t`, silently making
the quantified range empty or unexpectedly enormous.

### `assigns`: what a function leaves alone

`pre` and `post` say what a function needs and what it produces. Neither says
what it leaves *alone*, and a caller needs to know that. For example:

```c
int flag = 1;
decode(dst, dstSize, src, srcSize);
// is flag still 1?
```

With only `pre` and `post`, nobody can say. A checker has to assume `decode` may
have written to every object it could reach: `flag`, every global, everything
reachable through the pointers it was handed. So every fact the caller had
established before the call is thrown away, and the only way to recover it is to
go read the body of `decode`. That is exactly what a contract should spare you.

The `assigns` clause is the fix. Repeated clauses list the only locations the
function is *allowed* to write:

```c
size_t decode(void *dst, size_t dstSize, const void *src, size_t srcSize)
  pre (readable(src, srcSize))
  pre (writable(dst, dstSize))
  assigns (range((char *)dst, 0, dstSize));
```

Read that as: this may write those bytes of `dst`, and nothing else in the
program changes. `flag` is not on the list, so `flag` is still 1. The guarantee
comes from what is *absent* from the list, not from what is on it. (Verification
papers and the ACSL and CBMC manuals call this a *frame condition*, and the
listed locations the function's *frame*. Same idea, older name.)

The body of `decode` still gets checked, just separately, and that separation is
the point. There are two jobs. Verifying `decode` checks its body against its
own contract, once: it really does write nothing outside that range. Verifying a
*caller* then uses only the contract, and never opens the body. So two hundred
call sites cost two hundred checks against four lines of contract, not two
hundred re-analyses of `decode`, and a caller can be checked when all you have
is the header.

In a range, the second number is where it stops, not the last element covered:

```c
void clear3(int *buf)
  assigns (range(buf, 0, 3));   // buf[0], buf[1], buf[2]. Not buf[3].
```

Same convention as `for (i = 0; i < 3; i++)`, so the number you write is the one
already in your loop header. It counts elements, not bytes: on an `int *` that
range is three ints, twelve bytes, and doing the `sizeof` multiply CBMC wants is
the compiler's job, not yours.

An empty list, `assigns ()`, is legal, and says the function writes nothing at
all. It is the strongest thing you can claim, not a syntax error.

Note that `assigns` grants permission, it does not impose an obligation. A
function may write less than its list allows. All of the force is in what the
list leaves out.

Which is why the direction of a mistake matters. Listing too *much* is merely
weak: it is still true, callers just learn less than they could. Listing too
*little* is a false statement about the function. Nothing in the front end can
catch it, because at a call site the front end sees only the declaration; it is
caught when the verifier checks the body, which is the first of the two jobs
above. Skip that job and name half the buffer `decode` actually fills, and every
caller gets checked against a promise that does not hold.

#### `writable` is not `assigns`

They look alike, but they are asked of different parties. `writable` is an
obligation on the caller, and says nothing about whether the function writes a
byte of that memory. `assigns` is a promise by the callee.

| | who owes it | question it answers |
|---|---|---|
| `pre (writable(dst, n))` | caller | may it legally write here? |
| `assigns (range(dst, 0, n))` | callee | what could have changed? |

Each can cover ground the other does not:

```c
// The caller must own the whole buffer, but only the first 8 bytes change.
// 'writable' covers n; 'assigns' covers 8. A caller keeps everything it knew
// about bytes 8 through n.
void set_header(void *buf, size_t n)
  pre (writable(buf, n))
  assigns (range((char *)buf, 0, 8));

// And the other way: a global, which no 'writable' could mention.
unsigned g_count;
void bump_counter(void)
  assigns (g_count);
```

Where they do meet: anything written through a pointer parameter needs both, an
`assigns` entry so callers know it changed, and a `writable` precondition so the
write is legal in the first place. A global needs only the `assigns`, since
static storage is always there.

`writable` is permission to touch, `assigns` is a bound on what was touched.

### `loop_invariant`: what stays true every time around

A verifier handles a loop by unwinding it a fixed number of times, `--unwind N`,
which proves the loop correct up to `N` iterations and says nothing past that.

An invariant replaces that bound with induction. Show it holds on entry, and
that one arbitrary iteration preserves it, and it holds on every iteration, for
any `N`:

```c
void zero(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    assigns (locations(i, range(buf, 0, len)))
    loop_invariant (i <= len)
  {
    buf[i] = 0;
    i++;
  }
}
```

A loop declares what it assigns just as a function does, and for the same
reason: everything outside that list is unchanged by the loop.

### `decreases`: why the loop ends

An invariant proves the loop is right *if* it finishes. `decreases` is what
proves it finishes: an expression that strictly drops on every iteration and
never goes negative, so it cannot drop forever.

```c
void zero(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    assigns (locations(i, range(buf, 0, len)))
    loop_invariant (i <= len)
    decreases (len - i)
  {
    buf[i] = 0;
    i++;
  }
}
```

Here `len - i` shrinks by one each time around and stops at zero.

### Annotation reference

The core spellings are below. The full syntax and semantics are available in
**[docs/contracts-reference.md](docs/contracts-reference.md)**.

| Keyword | Goes | Says |
|---|---|---|
| `pre` | after a function's parameter list | must hold when the function is **called** |
| `returns` | after a function's parameter list | must hold when it **returns**; `c_result` names the result |
| `post` | after a function's parameter list | a result-independent fact that holds when it returns |
| `old` | only inside a postcondition | the value an expression had **on entry** |
| `loop_invariant` | between a loop's header and its body | true on entry and **preserved by every iteration** |
| `decreases` | between a loop's header and its body | **strictly decreases**, never negative — so the loop terminates |
| `assigns` | after a function's parameter list, or in a loop header | the **only** locations the function may modify |
| `c_ghost` | on a local declaration | the value exists for the contract only, and compiles away with it |
| `c_forall` | inside any predicate | `c_forall(i, lo, hi, P)` holds `P` for every `i` from `lo` up to, but not including, `hi` |

`assigns` takes one argument rather than a predicate. Use
`locations(a, b)` inside it to combine locations; nested `locations` calls
handle larger frames. A location may be a range: `range(buf, 0, len)` counts
elements and stops just before `len`.

These are *contextual* keywords, active only under `-fc-contracts`; without it
they are ordinary identifiers, which is why annotated source still builds with
any compiler. Writing them unprefixed takes `#define C_CONTRACTS_NO_PREFIX`
before the header, as
[Two spellings](#two-spellings-and-which-to-write) covers; the `c_`-prefixed
forms mean the same thing and are what a project writes if it prefers a
namespace.

For why these spellings rather than the verifier's `requires` and `ensures`, see
[contracts-design.md](contracts-design.md#5-syntax).


## License

Apache License v2.0 with LLVM Exceptions, the same as the rest of the LLVM
project. See [LICENSE.TXT](LICENSE.TXT).

---

# The LLVM Compiler Infrastructure

[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/llvm/llvm-project/badge)](https://securityscorecards.dev/viewer/?uri=github.com/llvm/llvm-project)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8273/badge)](https://www.bestpractices.dev/projects/8273)
[![libc++](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml/badge.svg?branch=main&event=schedule)](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml?query=event%3Aschedule)

Welcome to the LLVM project!

This repository contains the source code for LLVM, a toolkit for the
construction of highly optimized compilers, optimizers, and run-time
environments.

The LLVM project has multiple components. The core of the project is
itself called "LLVM". This contains all of the tools, libraries, and header
files needed to process intermediate representations and convert them into
object files. Tools include an assembler, disassembler, bitcode analyzer, and
bitcode optimizer.

C-like languages use the [Clang](https://clang.llvm.org/) frontend. This
component compiles C, C++, Objective-C, and Objective-C++ code into LLVM bitcode
-- and from there into object files, using LLVM.

Other components include:
the [libc++ C++ standard library](https://libcxx.llvm.org),
the [LLD linker](https://lld.llvm.org), and more.

## Getting the Source Code and Building LLVM

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM.

For information on how to contribute to the LLVM project, please take a look at
the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Getting in touch

Join the [LLVM Discourse forums](https://discourse.llvm.org/), [Discord
chat](https://discord.gg/xS7Z362),
[LLVM Office Hours](https://llvm.org/docs/GettingInvolved.html#office-hours) or
[Regular sync-ups](https://llvm.org/docs/GettingInvolved.html#online-sync-ups).

The LLVM project has adopted a [code of conduct](https://llvm.org/docs/CodeOfConduct.html) for
participants to all modes of communication within the project.
