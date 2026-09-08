# nghttp2 `nghttp2_buf_reserve`, proved through the grammar

[Finding 10](../../FINDINGS.md) was found by reading a detector hit. This proves
it with the branch's own pipeline, on a codebase neither the extension nor any
harness in this repo had seen before.

## The contract, written from the header's own comments

`nghttp2_buf.h` states the invariants in prose and checks none of them:

> the effective range of buffer is `[begin, end)` ... `pos <= last` must be hold
> ... `last <= end` must be hold ... Mark arbitrary position in buffer
> `[begin, end)`

Transcribed onto the function:

```c
int nghttp2_buf_reserve(nghttp2_buf *buf, size_t new_cap, nghttp2_mem *mem)
    c_pre (buf != NULL)
    c_pre (buf->begin <= buf->end)
    c_pre (buf->begin <= buf->pos && buf->pos <= buf->last)
    c_pre (buf->last <= buf->end)
    c_pre (buf->mark == NULL || (buf->begin <= buf->mark && buf->mark <= buf->end))
    c_assigns (buf->begin)
    c_assigns (buf->end)
    c_assigns (buf->pos)
    c_assigns (buf->last)
    c_assigns (buf->mark)
```

Five `c_pre` and five `c_assigns` clauses, all lowered by
`-fc-contracts -fcontract-emit-cprover-unit`, `goto-cc` clean.

## Result

`goto-instrument --enforce-contract nghttp2_buf_reserve`, then CBMC:

```
[nghttp2_buf_reserve.pointer_arithmetic.15] line 78 pointer relation:
    deallocated dynamic object in buf->pos:   FAILURE
[nghttp2_buf_reserve.pointer_arithmetic.21] line 78 pointer relation:
    deallocated dynamic object in buf->begin: FAILURE
[... .27/.33 line 79 buf->last, buf->begin ...]
[... .39/.45 line 80 buf->mark, buf->begin ...]
** 6 of 1509 failed (2 iterations)
```

Six failures, one per operand of the three subtractions:

```c
buf->pos  = ptr + (buf->pos  - buf->begin);
buf->last = ptr + (buf->last - buf->begin);
buf->mark = ptr + (buf->mark - buf->begin);
buf->begin = ptr;                            /* three lines too late */
```

That is the finding, reproduced with the contract carrying the preconditions
rather than a hand-written `__CPROVER_assume` block.

**This is the first end-to-end pass of the whole pipeline on a codebase chosen
after the tooling was written.** `--enforce-contract` succeeded here because
`nghttp2_buf_reserve` is loop-free, which is exactly the condition
[ISSUES.md#1](../../contracts-eval/ISSUES.md) says the zstd and expat attempts
failed on. Picking a loop-free function was deliberate.

## Issue found on the way: `c_pre` without `c_assigns` enforces an empty frame

The first run, with the five `c_pre` clauses and no `c_assigns`:

```
[nghttp2_buf_reserve.assigns.3] line 75 Check that buf->pos is assignable: FAILURE
[nghttp2_buf_reserve.assigns.4] line 76 Check that buf->last is assignable: FAILURE
[nghttp2_buf_reserve.assigns.5] line 77 Check that buf->mark is assignable: FAILURE
[nghttp2_buf_reserve.assigns.6] line 78 Check that buf->begin is assignable: FAILURE
[nghttp2_buf_reserve.assigns.7] line 79 Check that buf->end is assignable: FAILURE
```

A function contract with no `c_assigns` is enforced with an **empty** frame, so
every write the function makes is a violation. The five failures are not about
the defect at all -- they are the absence of a clause. A user annotating their
first function will write `c_pre` and hit this immediately, and the message points
at their code rather than at the missing clause.

See [ISSUES.md#7](../../contracts-eval/ISSUES.md).
