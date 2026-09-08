# `ZSTD_execSequence` through the grammar: what worked, and the blocker it names

[Finding 7](../zstd/findings/FINDING-execsequence-implicit-preconditions.md)
says `ZSTD_execSequence`'s preconditions exist only in asserts that `-DNDEBUG`
deletes, plus allocation arithmetic two call frames away. This writes them down
in the grammar, on the real function, and pushes them through to CBMC.

## Written on the function

```c
size_t ZSTD_execSequence(BYTE* op,
    BYTE* const oend, seq_t sequence,
    const BYTE** litPtr, const BYTE* const litLimit,
    const BYTE* const prefixStart, const BYTE* const virtualStart, const BYTE* const dictEnd)
    c_pre (op != NULL)
    c_pre (oend - op >= WILDCOPY_OVERLENGTH)
    c_pre (sequence.matchLength >= 1)
    c_pre (sequence.offset >= 1)
    c_pre (sequence.offset <= (size_t)(op - prefixStart) + sequence.litLength)
    c_pre (*litPtr + sequence.litLength <= litLimit)
    c_pre (sequence.litLength + sequence.matchLength <= (size_t)(oend - op))
```

Full text in [`patches/execsequence-preconditions.txt`](../zstd/patches/execsequence-preconditions.txt).
The matching harness,
[`harness_execsequence_from_grammar.c`](../zstd/harnesses/harness_execsequence_from_grammar.c),
is the existing `harness_execsequence.c` with those seven `__CPROVER_assume`
lines **deleted** -- only the harness's own domain bounds (`litLength <= LIT_CAP`,
`matchLength <= 16`) remain. Everything the function requires is now stated on
the function.

## What worked

Preprocessed zstd through `-fcontract-emit-cprover-unit` with the two existing
loop-contract patches also applied:

```
requires: 9   loop clauses: 21
```

Nine `__CPROVER_requires` (seven here plus the two on `BIT_getMiddleBits` /
`BIT_lookBits`) and twenty-one lowered loop clauses, and `goto-cc` accepts the
rewritten unit with no errors. The grammar handles the real decode path.

## The blocker, now established

`goto-instrument --apply-loop-contracts --enforce-contract ZSTD_execSequence`
fails:

```
Invariant check failed
File: src/goto-instrument/contracts/contracts.cpp:1167
      function: check_frame_conditions_function
Condition: is_loop_free(function_body, ns, log)
Reason: Loops remain in function 'ZSTD_execSequence', assigns clause checking
        instrumentation cannot be applied.
```

**`--enforce-contract` requires the function body to be loop-free after loop
contracts are applied.** `ZSTD_execSequence` ends in `ZSTD_safecopy`, which is
inlined, and `--show-loops` reports loops still remaining at
`zstd_decompress_block.c:1094` -- the `ZSTD_safecopy` call site -- after the
existing patches. The wildcopy loop is annotated; safecopy's are not, or not
all of them.

This is worth recording precisely because
[UNBOUNDED.md](../zstd/UNBOUNDED.md) currently says `ZSTD_safecopy` is unproved
and that **"its actual blocker is unestablished"**, the previous guess
(FORCE_INLINE) having been recorded as wrong. It is established now, and it is
not about `ZSTD_safecopy` in isolation:

> Every loop reachable in the inlined body of a function must carry a loop
> contract before that function's own contract can be enforced. Annotating
> `ZSTD_execSequence` is therefore gated on finishing `ZSTD_safecopy`, not
> merely adjacent to it.

That makes the safecopy loop contracts the next piece of work rather than an
optional one, and it explains why the two have kept turning up together.

## Second time today

`--enforce-contract` also fails on expat `storeRawNames`, there with
`Recursive call to 'callUnknownEncodingConvert' during inlining`
([write-up](../generalize/expat/RESULT-grammar-end-to-end.md)). Two real
functions, two different `goto-instrument` limits, same conclusion: **the
lowering is solid and the enforcement step is the fragile half of the
pipeline.** Both are bucket 3 in the annotation-yield taxonomy -- they make the
tool better, not the code.

Where enforcement does work, the clause is decisive: on `BIT_getMiddleBits` the
proof passes with `c_pre (nbBits < 32)` and finds an out-of-bounds table read
without it ([result 0](RESULT-what-the-clauses-actually-catch.md)).
