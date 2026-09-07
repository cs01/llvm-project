# expat `storeRawNames`: the old buffer pointer is read after `realloc` frees it

**Where:** `expat/lib/xmlparse.c`, `storeRawNames()`, lines 3118-3128 at
libexpat `a2d6433` (2026-09-07, current HEAD).

**Class:** use of an indeterminate pointer value (C17 6.2.4p2). Same family as
[the zstd findings](../../zstd/findings/): a pointer that is never dereferenced,
so nothing at runtime objects.

## The code

```c
char *temp = REALLOC(parser, tag->buf.raw, bufSize);
if (temp == NULL)
  return XML_FALSE;
if (tag->name.str == tag->buf.str)                    /* (1) */
  tag->name.str = (XML_Char *)temp;
if (tag->name.localPart)
  tag->name.localPart
      = (XML_Char *)temp + (tag->name.localPart - tag->buf.str);   /* (2) */
tag->buf.raw = temp;                                  /* too late */
```

`buf.raw` and `buf.str` are the two arms of one union, so they are the same
pointer. When `realloc` moves the block it deallocates the old one, and
`tag->buf.str` becomes indeterminate at that instant. Line (1) then compares
that value and line (2) uses it as the right operand of a pointer subtraction.
`tag->buf.raw = temp` repairs it two lines after the last read.

The offset line (2) computes is numerically correct on every real target, which
is why this has survived: the code does the right arithmetic with a value it is
not permitted to look at.

## Why nothing caught it

Nothing dereferences the stale pointer. ASan instruments loads and stores, so a
freed pointer that is only compared and subtracted passes through it silently,
and no fuzzer input distinguishes the moved-block case from the in-place one --
`realloc` chooses. This is the same reason the two zstd defects sat in shipped
code, and it is the argument for asking a prover instead of a sanitizer.

## Proof

[`harness_storerawnames.c`](harness_storerawnames.c) builds a one-entry tag
stack with a symbolic name length, a symbolic `localPart` offset into `tag->buf`,
and a `rawName` long enough to force the realloc branch.

```
goto-cc -I. -Ilib -DHAVE_EXPAT_CONFIG_H -o hs0.goto harness_storerawnames.c --function harness
../../solve.sh -t 600 hs0.goto --function harness \
    --pointer-check --bounds-check --conversion-check --unwind 24
```

```
[storeRawNames.pointer_arithmetic.15] line 3127 pointer relation:
    deallocated dynamic object in tag->name.localPart: FAILURE
[storeRawNames.pointer_arithmetic.21] line 3127 pointer relation:
    deallocated dynamic object in byte_extract_little_endian(tag->buf, 0l, XML_Char *): FAILURE
** 2 of 20740 failed (2 iterations)
VERIFICATION FAILED
== solved by sat in 2s (rc 10)
```

Both operands of the one subtraction, and nothing else in 20740 obligations.

The harness sets `XML_GE 0` (and so drops `XML_DTD`) only to route `REALLOC`
through `m_mem.realloc_fcn` instead of expat's accounting wrapper. Under
`XML_GE 1` the same two properties fail, alongside noise from the zeroed
`m_alloc_tracker` in the stub parser. The defect does not depend on the setting:
`expat_realloc` calls `realloc_fcn` and the block moves either way.

The harness deliberately does not call `XML_ParserCreate` -- that pulls in the
hash tables and the random seed, and symbolic execution never comes back out.
`storeRawNames` reads two fields of the parser, so the harness supplies two.
The first attempt at this proof ran `--z3` for ten minutes without reaching a
solver at all; see [`solve.sh`](../../solve.sh), which now reports the phase each
solver died in so that a symex-bound harness cannot be mistaken for a hard one.

## Fix

Move the assignment above the reads, and take the offset before the call:

```c
const ptrdiff_t localPartOffset
    = tag->name.localPart ? tag->name.localPart - tag->buf.str : 0;
const XML_Bool nameInBuf = (tag->name.str == tag->buf.str);
char *temp = REALLOC(parser, tag->buf.raw, bufSize);
if (temp == NULL)
  return XML_FALSE;
tag->buf.raw = temp;
if (nameInBuf)
  tag->name.str = (XML_Char *)temp;
if (tag->name.localPart)
  tag->name.localPart = (XML_Char *)temp + localPartOffset;
```

Same instructions, no read of a freed value. Not yet reported upstream.

## The second, weaker one in the same function

```c
char *rawNameBuf = tag->buf.raw + nameLen;
```

is computed at the top of the loop body, before the
`bufSize > (size_t)(tag->bufEnd - tag->buf.raw)` test that establishes the
buffer is large enough. If `nameLen` can exceed `tag->bufEnd - tag->buf.raw`,
that forms a pointer more than one past the end, which is UB on its own.
Whether it can is an invariant of the callers and is written down nowhere --
which is the precondition this branch's grammar exists to state. Not yet
proved either way; recorded so it is not lost.
