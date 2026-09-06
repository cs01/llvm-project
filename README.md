# C contracts for clang (`contracts-c-dev`)

> A branch of [cs01/llvm-project](https://github.com/cs01/llvm-project) that adds
> `-fc-contracts`: preconditions, postconditions and loop contracts for C as
> **real grammar in the compiler**, checked at compile time and lowered to CBMC
> so they can be *proved*. Upstream LLVM's README follows below.
>
> The fork's other line of work, flow-sensitive nullability, is independent and
> lives on
> [`nullsafe-clang-dev`](https://github.com/cs01/llvm-project/tree/nullsafe-clang-dev).

## The keywords

Five words and one flag. That is the whole surface a developer has to learn.

| Keyword | Goes | Takes | Says |
|---|---|---|---|
| `pre` | after a function's parameter list | a condition | must be true when the function is **called** |
| `post` | after a function's parameter list | a condition, optionally naming the result | must be true when the function **returns** |
| `old` | only inside a `post` | a scalar expression | the value that expression had **on entry** |
| `invariant` | between a loop's header and its body | a condition | true on entry to the loop and **preserved by every iteration** |
| `variant` | between a loop's header and its body | an integer measure | **strictly decreases** every iteration and never goes negative, so the loop must end |

`writes` is reserved: it parses and is then rejected with "not supported yet",
so the grammar is pinned now and cannot be quietly redefined later.

None of these are `__attribute__`, macros, or comments. They are contextual
keywords, active only under `-fc-contracts`, so a program that uses `pre` or
`invariant` as an ordinary identifier keeps compiling.

### Grammar

Function clauses attach to the **declarator**, after the parameter list, on
either a prototype or a definition:

```
function-declarator:
        declarator '(' parameter-list ')' contract-clause-seq[opt]

contract-clause:
        'pre'    '(' expression ')'
        'post'   '(' result-binding[opt] expression ')'
        'writes' '(' ... ')'                    // reserved; hard error today

result-binding:
        identifier ':'
```

Loop clauses sit between the loop header and the body:

```
iteration-statement:
        'while' '(' expression ')' loop-clause-seq[opt] compound-statement
        'for'   '(' ... ')'        loop-clause-seq[opt] compound-statement
        'do'    loop-clause-seq[opt] compound-statement 'while' '(' expr ')' ';'

loop-clause:
        'invariant' '(' expression ')'
        'variant'   '(' expression ')'
```

and `old` is an expression form, legal only inside a `post`:

```
old-expression:
        'old' '(' expression ')'
```

### `pre`, `post`, `old`

```c
unsigned long decompress(void *dst, unsigned long dstCap,
                         const void *src, unsigned long srcSize)
  pre  (dst != 0)
  pre  (dstCap > 0)
  post (r: r <= old(dstCap) || is_error((int)r));
```

Clauses repeat; each one is a separate obligation.

#### The `r:` part — naming the return value

C has no way to *say* "the value this function returns". `return` is a
statement, and the result has no name you can write in an expression. So a
`post` that wants to constrain the result has to introduce a name for it, and
that is all the `r:` is:

```c
post (r: r <= old(dstCap))
//    ^  ^
//    |  the predicate, which may now use that name
//    the binding: "call the return value r for this clause"
```

The name before the colon is **declared** there; every use after the colon
refers to it. It is not a magic identifier — pick whatever reads best:

```c
unsigned long decompress(...) post (written: written <= old(dstCap));
int *allocate(unsigned long n) post (p: p != 0);
```

Two properties worth knowing:

- **It is optional.** A `post` that does not need to mention the result just
  omits it: `post (errno_is_clean())`. The `identifier :` prefix is only parsed
  when an identifier is immediately followed by a colon, so nothing is ambiguous.
- **It is scoped to its own clause.** The binding lives on the `ContractClause`,
  not on the function, so each `post` that wants the result names it again, and
  two clauses may use different names. This is also why the name cannot collide
  with anything: it does not outlive the parentheses it appears in.

Under `-fcontract-emit-cprover` the bound name is renamed to CBMC's
`__CPROVER_return_value`, which is the same concept with a fixed spelling. C++26
contracts (P2900) solve it the same way. The rename is whole-token, so a
parameter named `rate` is not mangled by a binding named `r`.

`old(dstCap)` names the value `dstCap` held at entry, and it is **required**
rather than optional. In C every parameter is a by-value copy that the body may
freely mutate (`src += 4`, `dstCap -= n` are ordinary in codec code), so a
`post` naming a bare parameter would be silently ambiguous between its entry and
exit value. Writing one is an error that tells you to use `old`. It takes
scalars only: snapshotting a struct is a copy, not an annotation.

### `invariant` and `variant`

An **invariant** is what stays true no matter how many times the loop runs. A
**variant** is what shrinks every time it runs. Together they are how a prover
handles a loop whose trip count it does not know, without unrolling it: the
invariant gives it induction, the variant gives it termination.

```c
void fill(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    invariant (i <= len)      // true before, and after every iteration
    variant   (len - i)       // gets smaller each time, never negative
  {
    buf[i] = 0;
    i++;
  }
}
```

Read `variant` as "the measure that runs out". `len - i` starts at `len`,
strictly decreases because `i` grows, and cannot go below zero because the
invariant bounds `i` by `len`. A quantity that can only decrease finitely many
times is a loop that must stop — that, and nothing more, is what a variant
proves. Leave it off and you can still prove the loop *correct*, just not that
it *ends*.

Without an invariant, a verifier's only options on a loop are to unroll it a
fixed number of times, which proves something about small inputs only, or to
give up and assume nothing. With one, `ZSTD_wildcopy` is proved memory-safe for
**every** length rather than up to some bound — see the zstd results below.

### The rules that will bite you

1. **A loop with clauses must brace its body.** `while (x) invariant(x);` is
   already a valid C call statement in some codebase somewhere, so the parser
   commits to the contract reading only when a `{` follows the clause sequence.
   Otherwise the tokens stay exactly what they were before this extension
   existed. The cost is that `for (...) invariant (i < n) stmt;` is not a
   contract; brace it.
2. **Predicates must be pure.** No assignment, no `++`, and calls only to
   functions marked `const` or `pure`. Every tier evaluates a predicate more
   than once, so a predicate with a side effect is a predicate that means
   different things to different tiers. Those two existing GCC/clang attributes
   serve as the "usable in a spec" marker rather than a new one being invented.
3. **Contracts cannot be restated on a redeclaration.** Comparing two predicates
   written against two different sets of `ParmVarDecl`s for equivalence is not
   implemented, and silently keeping one of them would make *which declaration a
   caller happened to include* change what gets checked.
4. **A macro named `pre`, `post`, `invariant`, `variant` or `old` shadows the
   keyword**, and warns (`-Wc-contracts`) rather than erroring, because a
   project may have an unrelated `pre` macro and never write a contract.

### Flags

| Flag | Effect |
|---|---|
| `-fc-contracts` | turn the keywords on. Without it they are ordinary identifiers |
| `-fcontract-emit-cprover` | print the contracts as CBMC clauses on stdout |
| `-Wcontract-violation` | the call-site violation warning (on by default, inside `-Wc-contracts`) |

`__has_feature(c_contracts)` is true under the flag, so a header can carry
contracts and still compile with a stock clang.

## How it works inside clang

Four stages, each a real part of the compiler rather than a preprocessor trick.

**1. Parsed as contextual keywords.** `pre` and `post` are read in
`ParseFunctionDeclarator`, in the declarator suffix, with the parameters already
in scope — which is what lets a predicate name them. `post` is special-cased:
its predicate may bind the return value, and the return type is not known while
the declarator is still being built (for `int *f(void)` the pointer chunk is
added *after* the function chunk), so its tokens are saved and replayed once the
`FunctionDecl` exists. Loop clauses are read in `ParseStmt` between the header
and the body, using a token-stream lookahead — the parser scans balanced parens
past the clause sequence and only commits if a `{` follows.

**2. Type-checked and stored in the AST.** A `pre`, `post` or `invariant`
predicate goes through the same contextual boolean conversion `if` uses, so
`pre (p)` on a pointer means `pre (p != 0)`. A `variant` is a *measure*, not a
condition, so it keeps its own arithmetic type and is only required to be
scalar. Purity is enforced with `Expr::HasSideEffects`. Function clauses land in
a `ContractSpecifier` hanging off the `FunctionDecl` and survive a PCH; loop
clauses live in an `ASTContext` side table keyed by the loop statement, because
contracts are rare and `WhileStmt` is among the most numerous nodes in any AST —
paying eight bytes on every loop in every program to store nothing was the wrong
trade.

**3. Checked at compile time by a CFG dataflow pass.**
`clang/lib/Analysis/ContractChecking.cpp` is a standalone forward dataflow
analysis over clang's `CFG`, run from `AnalysisBasedWarnings` like any ordinary
warning:

```
demo.c:16:12: warning: precondition cap > 0 of 'buf_new' is violated by this call
demo.c:3:3:   note: precondition declared here
```

The lattice is deliberately tiny — a variable is a known integer, known null,
known non-null, or unknown. It walks blocks in reverse post-order, refines state
along each branch edge by the condition that got there, and **merges by keeping
only what every predecessor agrees on**: disagreement means the pass does not
know, and not knowing must never produce a report. Back edges are not iterated
to a fixpoint, which costs missed reports and never invents one. Variables whose
address is taken are never tracked at all, which is the cheap defence against
the out-parameter false positive (`T *p = 0; f(&p); p->x;`) that dominates this
class of analysis. The function's own `pre` clauses are seeded as true on entry,
so a call inside the body can be discharged by a guarantee the caller already
made, and a callee's `post` is assumed after a call.

The pass reports only preconditions it can show are **violated** — never ones it
merely cannot prove. That is the difference between a warning developers leave
on and one they turn off.

**4. Emitted for CBMC.** `-fcontract-emit-cprover` prints the same clauses as
[CBMC](https://github.com/diffblue/cbmc) contracts, close to one for one, which
is the whole argument for targeting an existing verifier instead of building
one:

| Clause | Becomes |
|---|---|
| `pre (P)` | `__CPROVER_requires(P)` |
| `post (r: P)` | `__CPROVER_ensures(P)` with `r` renamed to `__CPROVER_return_value` |
| `old (e)` | `__CPROVER_old(e)` |
| `invariant (P)` | `__CPROVER_loop_invariant(P)` |
| `variant (m)` | `__CPROVER_decreases(m)` |
| `writes (...)` | `__CPROVER_assigns(...)` — once `writes` is implemented |

```
$ clang -fc-contracts -fcontract-emit-cprover -fsyntax-only decompress.c
/* decompress */
__CPROVER_requires(dst != 0)
__CPROVER_ensures(__CPROVER_return_value <= __CPROVER_old(dstCap))
```

Function clauses are emitted from the declarator; loop clauses only once the
body has been parsed, since that is the first point they are reachable. The
effect is that a function's `requires` and `ensures` print ahead of the loops
they scope. `goto-instrument --enforce-contract` and `cbmc` then discharge them.

One back-end wart worth knowing: **goto-instrument rejects loop contracts on
`do` outright.** The grammar here deliberately keeps no such restriction, so the
emitter prints the clauses with a note that the loop needs the mechanical
`do { B } while (C)` → `while (1) { B; if (!C) break; }` rewrite first. It does
not perform the rewrite, because this mode emits clauses, not source.

### What each tier actually proves

Being precise about this matters more than the feature list:

- **The compile-time warning proves nothing.** It is unsound and incomplete by
  construction. It finds real bugs at call sites and is worth having on by
  default; it is not a guarantee.
- **CBMC proves the real thing**, but only what you asked: "given the
  preconditions, the postconditions hold and there is no UB, in this function."
  A wrong `post` is proved happily. It proves the code matches the spec, never
  that the spec is right.
- **There is no runtime-trap tier yet.** Nothing lowers a contract to a branch
  in codegen today. And note that the interesting clauses could not be traps
  anyway: `p != NULL` or `n > 0` compile to a branch fine, but "p points to at
  least n readable bytes" cannot, because there is no way to recover the
  allocation behind a `void *` at function entry. Buffer and frame clauses are
  documentation that the static tiers consume, not runtime checks.

See [`contracts-design.md`](contracts-design.md) for the full design, the
rejected alternatives, and why the SMT-solver route inside clang was cut.

## Building it

Ordinary LLVM build; nothing bespoke. Use clang to build it, not gcc:

```sh
cmake -G Ninja -S llvm -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=X86 \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_USE_LINKER=lld \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DLLVM_OPTIMIZED_TABLEGEN=ON
ninja -C build clang
```

Then the contract tests:

```sh
ninja -C build check-clang-sema check-clang-parser
build/bin/llvm-lit -v clang/test/Sema/c-contracts-cprover-loop.c
```

## Results on real zstd

See **[proofs/zstd/](proofs/zstd/)**. Applied to the actual zstd sources, not a
transcription:

- **Two undefined-behaviour findings.** `ZSTD_overlapCopy8` forms a pointer up to
  8 bytes before the start of the output buffer, reachable with a legal stream
  (three-line fix, proved). `ZSTD_wildcopy` and `ZSTD_safecopy` subtract pointers
  their own doc-comments say are in different objects. Neither is exploitable on
  conventional hardware; both break CHERI and are freedoms a
  provenance-exploiting optimiser may take.
- **Functional correctness of the LZ reconstruction.** Not just "stays in
  bounds": `ZSTD_execSequence` provably emits the *right bytes*, including for
  matches that overlap their own output. This is the layer where "your data comes
  back" lives.
- **Two contract defects** where the real precondition is stronger than the
  documented one, or absent from where it is needed entirely.
- **An unbounded proof.** `ZSTD_wildcopy` is memory-safe for *every* length, not
  just up to some bound: `0 of 141 failed`, one iteration, no `--unwind` at all,
  length to 1 GiB with symbolically allocated buffers. Proof by induction over
  the loop, via a hand-written `__CPROVER_loop_invariant`. The same run also
  proves the fix for the pointer-subtraction defect: with it, zero failures;
  without it, exactly one.

None of the four is a crash, and **fuzzing cannot find any of them**, because
nothing misbehaves at runtime. That, rather than the count, is the argument.
A fifth check — the `FSE_readNCount` bounds audit — came back clean, and the
negative result is recorded too.

Read **[proofs/zstd/COST.md](proofs/zstd/COST.md)** before estimating anything:
solve time is driven by symbolic state size, not obligation count, and it decides
whether verifying a codec is a quarter or a research program.

## What is not done

Loop `invariant` / `variant` parse on `while`, `for`, and `do`, land in the AST,
and lower to `__CPROVER_loop_invariant` / `__CPROVER_decreases`. What still
stands between that and turning the `proofs/zstd/` patches into source is
`writes`, which is diagnosed as unimplemented: every hand-written annotation
there also carries an `__CPROVER_assigns` frame that has no source syntax yet.
The `do` loops need one more step even so, since goto-instrument rejects loop
contracts on `do`; the emitter says so on each one rather than rewriting the
loop, because it emits clauses, not source.

Most results are still bounded: exhaustive over a small domain rather than
universal. `ZSTD_wildcopy` is the exception and shows the route out.
`proofs/zstd/UNBOUNDED.md` documents that route and, more usefully, the four
obstacles hit on the way, none of which is about mathematics:

- CBMC rejects loop contracts on `do`/`while`; the loop must be rewritten.
- `do { } while (0)` macros count as loops, so a contract silently attaches to
  the wrong one. No diagnostic.
- The `assigns` clause havocs the cursors before the invariant is assumed, so
  raw pointer comparisons in an invariant get flagged themselves. Use
  `__CPROVER_same_object` and `__CPROVER_POINTER_OFFSET`.
- A symbolic extent in `assigns` generates its own unbounded havoc loop. Use a
  concrete bound where the semantics give you one.
- **`FORCE_INLINE` functions defeat callee contracts.** `ZSTD_wildcopy` is
  inlined into `ZSTD_safecopy` before contracts are applied, so the invariant
  written on the standalone function does not transfer. Loop contracts are per
  loop *instance*, so an inlined loop needs its invariant repeated at every site.
  The decoder has fifteen `FORCE_INLINE` uses, which multiplies the annotation
  burden rather than adding a fixed cost. This is the one that does not go away
  with a rewrite.

That list is the honest scoping input: the hard part of applying this to real C
is toolchain-versus-codebase fit, not proving things.

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
