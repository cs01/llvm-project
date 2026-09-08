# Does the grammar reproduce the expat finding end to end?

Every result in [`generalize/`](../README.md) so far was produced by hand-written
CBMC harnesses -- `__CPROVER_assume`, `nondet_*`, prover vocabulary throughout.
That is evidence about *CBMC plus a good harness*. It is not evidence about this
branch's extension, which exists so that a maintainer can write C instead.

So: annotate the real function in the grammar, lower it with the fork's clang,
and check whether the same defect still falls out the other end.

## What was annotated

`storeRawNames` in `expat/lib/xmlparse.c`, on the definition, in source a stock
clang still compiles (the keywords are ordinary identifiers without
`-fc-contracts`):

```c
static XML_Bool
storeRawNames(XML_Parser parser)
    c_pre (parser != NULL)
    c_pre (parser->m_tagStack == NULL || parser->m_tagStack->buf.raw <= parser->m_tagStack->bufEnd)
{
```

The second clause is the invariant
[the finding](FINDING-storerawnames-freed-pointer.md) says is written down
nowhere.

## Result: yes

Pipeline mirrors [`run-wildcopy-from-grammar.sh`](../../zstd/run-wildcopy-from-grammar.sh):
preprocess with the system compiler, rewrite `__builtin_mem*` to the plain names
CBMC models, prepend the `__CPROVER_assume` declaration, lower, compile, prove.

```
clang -cc1 -fsyntax-only -fblocks -fc-contracts -fcontract-emit-cprover-unit h3.i > sr.c
```

11424 lines of preprocessed expat in, rc 0, no errors, and both clauses lowered:

```c
    __CPROVER_requires(parser != ((void *)0))
    __CPROVER_requires(parser->m_tagStack == ((void *)0) || parser->m_tagStack->buf.raw <= parser->m_tagStack->bufEnd)
```

`goto-cc` accepts the rewritten unit, and it produces the identical result to
the hand-written harness:

```
[storeRawNames.pointer_arithmetic.15] line 3130 pointer relation:
    deallocated dynamic object in tag->name.localPart: FAILURE
[storeRawNames.pointer_arithmetic.21] line 3130 pointer relation:
    deallocated dynamic object in byte_extract_little_endian(tag->buf, 0l, XML_Char *): FAILURE
** 2 of 20740 failed (2 iterations)
```

Same two properties, same 20740 obligations, same verdict. Line 3130 rather than
3127 is the three added clause lines. The emitter carried a 11k-line real-world
translation unit through without losing the defect, on a codebase the extension
was never designed against.

## What this does not show

**The `c_pre` clauses are not what finds the bug.** The failures come from
`--pointer-check`, exactly as they do without any annotation. What the grammar
contributes here is that the precondition is now *stated, in C, in the source*,
and that the lowering is faithful. Claiming the contracts found this defect
would be false.

**Enforcement is unverified on this TU.** `goto-instrument --enforce-contract
storeRawNames`, which is what actually checks a `requires` clause rather than
merely carrying it, dies:

```
Recursive call to 'callUnknownEncodingConvert' during inlining
Numeric exception : 0
```

expat routes an unknown-encoding conversion through a function pointer that
lands back in the parser, and `--enforce-contract` inlines the reachable graph
eagerly. Not caused by `_FORTIFY_SOURCE`; tested with the `__builtin___*_chk`
expansions rewritten away and it fails identically. So the clauses are lowered
and syntactically live, and whether CBMC would enforce them on this function is
an open question, not a claim.

That is a toolchain limit worth its own line: the annotate-and-prove path works
on a real TU, and the enforce-the-contract path does not survive one yet.

## One process note

`build-arm/bin/clang` was two days and 70 commits stale and did not have
`-fcontract-emit-cprover-unit` at all -- `error: unknown argument`. Any
conclusion about whether the extension works, drawn without rebuilding first,
would have been drawn from a binary that did not contain it.
