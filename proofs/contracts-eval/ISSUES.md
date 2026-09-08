# Issues in our pipeline, found by dogfooding it on real code

Every entry here came from trying to verify an unmodified upstream function with
this branch's grammar, not from a synthetic test. That is the point: the toy in
[`e2e/case4_modular.c`](../zstd/e2e/case4_modular.c) passes, and none of these
appear until the same pipeline meets zstd or expat.

Ordered by how much they cost.

## 1. `--enforce-contract` requires a loop-free body, so function contracts are gated on loop contracts

Hit on `ZSTD_execSequence`:

```
Invariant check failed
File: src/goto-instrument/contracts/contracts.cpp:1167
Reason: Loops remain in function 'ZSTD_execSequence', assigns clause checking
        instrumentation cannot be applied.
```

`case4_modular.c` already notes this in a comment. What real code adds is the
*transitivity*: `ZSTD_execSequence`'s own body has no loops. The loops arrive
by inlining `ZSTD_safecopy`, which inlines `ZSTD_wildcopy`. So annotating a
function requires annotating every loop reachable through its inlined callees.

**What this means for us.** A user who writes `pre`/`assigns` on a leaf-looking
function gets an internal-invariant crash from `goto-instrument`, with no
indication that the fix is loop contracts several functions away. We should
detect this at the point the user can act on it: when `-fcontract-emit-cprover-unit`
emits a function contract for a function whose body (after the inlining the
verification build will do) contains an un-annotated loop, say so.

## 2. `FORCE_INLINE` silently discards a callee's contract

`ZSTD_wildcopy` is `MEM_STATIC FORCE_INLINE_ATTR`. Its contract does not
transfer to the copy inlined into `ZSTD_safecopy`, so a caller cannot be
verified against it and the callee's loop contracts do not apply either.

Suppressing the attribute in the verification build fixes it, and the effect is
visible in `--show-loops`: `ZSTD_wildcopy`'s three loops move out of
`ZSTD_safecopy` and into `ZSTD_wildcopy`, where its annotations can attach.

```c
#ifdef ZSTD_CONTRACTS
#  undef FORCE_INLINE_ATTR
#  define FORCE_INLINE_ATTR
#endif
```

**What this means for us.** This is a silent wrong answer, not an error: the
contract is accepted, lowered, and then quietly has no effect. Either the
compiler should refuse `assigns`/`pre` on an `always_inline` function under
`-fcontract-emit-cprover-unit`, or it should drop the attribute itself in that
mode. Doing nothing is the one option that misleads.

zstd has fifteen `FORCE_INLINE` uses across `zstd_decompress_block.c` and
`huf_decompress.c`, so this is structural, not incidental.

## 3. Pass ordering: `--drop-unused-functions` before contract instrumentation breaks it

`--drop-unused-functions` is needed for tractability (386 loops to 7 on the
`ZSTD_safecopy` harness, per UNBOUNDED.md). Run before
`--replace-call-with-contract`, it removes `malloc`, which CBMC's own contract
runtime needs:

```
line 24 function __CPROVER_replace_ensures_is_fresh: function 'malloc' is not declared
```

The result is `VERIFICATION ERROR` with **117 properties reported `ERROR`
rather than `SUCCESS` or `FAILURE`** -- every obligation unevaluated, which
reads at a glance like a proof that ran.

**What this means for us.** Any script we ship must instrument first and drop
second. `run-wildcopy-from-grammar.sh` does not hit this because it never drops.
Worth a line in the recipe, because the failure mode is a clean-looking `** 0 of
345 failed` next to a status nobody reads.

## 4. An `assigns` frame with over-copy slack needs a `pre` that nothing forces you to write

Our frame for `ZSTD_wildcopy`, which matches what the function actually writes:

```c
assigns (((BYTE*)dst)[0 : length + WILDCOPY_OVERLENGTH])
```

```
[ZSTD_wildcopy.assigns.1] Check that
   __CPROVER_object_upto(op, length + 32) is valid: ERROR
```

The frame claims the right to write 32 bytes past `length`. That is exactly what
`ZSTD_wildcopy` does by design, and it is only sound because callers guarantee
the slack -- the guarantee that
[finding 7](../zstd/findings/FINDING-execsequence-implicit-preconditions.md)
says is established two call frames away in `ZSTD_decodeLiteralsBlock` and
written down nowhere.

**This one is the checker working.** It refused a frame whose validity depends
on an unstated precondition, which is the whole thesis of the branch. The
missing clause is `pre(writable(dst, length + WILDCOPY_OVERLENGTH))`. Worth
keeping as the worked example of an `assigns` that forces a `pre` into
existence.

## 5. `goto-cc` cannot parse the arm64 SDK types clang's headers pull in

```
arm_vector_types.h:93:1: error: syntax error before 'mfloat8x8_t'
PARSING ERROR
```

`__mfp8`, `__bf16` and the `neon_vector_type` typedefs, reached through zstd's
`compiler.h`. Same class as the `_Float128` note in
[`run-wildcopy-from-grammar.sh`](../zstd/run-wildcopy-from-grammar.sh), which is
about glibc on x86; this is the Apple/arm64 counterpart. Workaround is
`-U__ARM_NEON -U__ARM_NEON__` plus filtering those declarations.

**What this means for us.** Every recipe we publish needs a preprocessing step
per platform, and ours currently documents only one. A `contracts-preprocess`
wrapper would be worth more than another finding.

## 6. `--enforce-contract` dies on a function-pointer cycle

On expat `storeRawNames`:

```
Recursive call to 'callUnknownEncodingConvert' during inlining
Numeric exception : 0
```

Not `_FORTIFY_SOURCE`; verified by rewriting the `__builtin___*_chk`
expansions away and reproducing. See
[the write-up](../generalize/expat/RESULT-grammar-end-to-end.md).

## 7. A function contract with `pre` but no `assigns` is enforced with an empty frame

Hit on the first annotation of `nghttp2_buf_reserve`, which had five `pre`
clauses and no frame:

```
[nghttp2_buf_reserve.assigns.3] line 75 Check that buf->pos is assignable: FAILURE
[nghttp2_buf_reserve.assigns.4] line 76 Check that buf->last is assignable: FAILURE
... one per field the function writes
```

Every write becomes a violation, and none of the five failures has anything to
do with the function. Adding `assigns(buf->begin, buf->end, buf->pos, buf->last,
buf->mark)` replaces all five with the six real ones.

**What this means for us.** This is the first thing a new user will hit, because
`pre` is the clause people reach for first and a frame is not obviously
required. The diagnostics point at their function body rather than at the
missing clause. We should warn at lowering time: a function contract that will
be enforced, carrying no `assigns` and whose body writes through a parameter,
is almost always incomplete rather than intentionally empty.

## Where this leaves the pipeline

The two halves have very different maturity, and the split is clean:

| Stage | On real code |
|---|---|
| Parse and check clauses in clang | works: expat 11k lines, zstd decode path |
| Lower to `__CPROVER_*` | works: 9 requires + 21 loop clauses on the zstd TU |
| `goto-cc` the rewritten unit | works, once the platform types are filtered (5) |
| `goto-instrument` contract instrumentation | **fragile: issues 1, 2, 3, 6**; works on a loop-free function ([nghttp2](../generalize/nghttp2/RESULT-proved-through-the-grammar.md)) |
| `cbmc` discharge | works where instrumentation succeeded |

Nothing here is a defect in the grammar or the emitter. Every one is in the step
between lowering and solving, and issues 1 and 2 are ones our compiler is better
placed to diagnose than `goto-instrument` is.
