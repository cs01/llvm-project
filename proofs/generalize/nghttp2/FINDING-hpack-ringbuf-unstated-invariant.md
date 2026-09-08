# nghttp2 HPACK ring buffer: the invariant every caller needs and nobody states

Found with the contract API rather than with a detector. No pattern matches
this; it only appears when you try to verify one function against another's
contract instead of by inlining it.

## The pair

`lib/nghttp2_hd.c`. Both preconditions ship as asserts, so `-DNDEBUG` removes
every check:

```c
static nghttp2_hd_entry *hd_ringbuf_get(nghttp2_hd_ringbuf *ringbuf, size_t idx) {
  assert(idx < ringbuf->len);
  return ringbuf->buffer[(ringbuf->first + idx) & ringbuf->mask];
}

nghttp2_hd_nv nghttp2_hd_table_get(nghttp2_hd_context *context, size_t idx) {
  assert(INDEX_RANGE_VALID(context, idx));
  if (idx >= NGHTTP2_STATIC_TABLE_LENGTH)
    return hd_ringbuf_get(&context->hd_table, idx - NGHTTP2_STATIC_TABLE_LENGTH)->nv;
  ...
}
```

`idx` is an HPACK table index, which comes off the wire.

Promoted to contracts:

```c
static nghttp2_hd_entry *hd_ringbuf_get(nghttp2_hd_ringbuf *ringbuf, size_t idx)
    c_pre (idx < ringbuf->len)
    c_pre (ringbuf->len <= ringbuf->mask + 1)

nghttp2_hd_nv nghttp2_hd_table_get(nghttp2_hd_context *context, size_t idx)
    c_pre (idx < context->hd_table.len + NGHTTP2_STATIC_TABLE_LENGTH)
    c_pre (context->hd_table.len <= context->hd_table.mask + 1)
```

## Result 1: the two preconditions agree

`--replace-call-with-contract hd_ringbuf_get --enforce-contract nghttp2_hd_table_get`
reports **no `requires` violation**. The caller's stated precondition does imply
the callee's, with the index arithmetic in between. That is a clean negative and
it is worth having: it is the check
[the `BIT_lookBits` mismatch](../../zstd/EXPERIMENT-annotation-yield.md) fails,
and nghttp2 passes it.

## Result 2: the accessor has no postcondition, so no caller can be verified

With the call replaced by its contract, six obligations fail at the caller:

```
[nghttp2_hd_table_get.pointer_dereference.7]  ... pointer NULL in return_value_hd_ringbuf_get->nv: FAILURE
[... .8 invalid, .9 deallocated, .10 dead object, .11 outside object bounds, .12 invalid integer address]
```

`hd_ringbuf_get` says nothing about what it returns, so a caller that
dereferences the result has nothing to lean on. Adding the missing clause:

```c
    c_returns (c_readable(c_result, sizeof(nghttp2_hd_entry)))
```

takes the caller's six failures to **zero**, and leaves two on `hd_ringbuf_get`:

```
[hd_ringbuf_get.pointer_primitives.1] pointer invalid in R_OK(return_value, 80): FAILURE
[hd_ringbuf_get.pointer_primitives.4] pointer outside object bounds in R_OK(...): FAILURE
```

That is modular verification behaving correctly: the caller is now provable from
the contract alone, and the obligation has moved to the function that owes it.

**The unstated invariant is: every live slot of the ring holds a readable
entry.** It is true -- `hd_ringbuf_push_front` only ever stores a real entry and
`hd_ringbuf_reserve` copies the live ones forward -- but it is written down
nowhere, and it is what every dereference of `nghttp2_hd_table_get`'s result
depends on. `c_returns (c_result != 0)` is not enough: it rules out NULL and leaves
liveness and bounds, which is why `readable` exists.

## Why this is not a bug, and is still the point

Nothing here is exploitable and nothing misbehaves. The invariant holds. What
the exercise establishes is that **two of the three facts this code depends on
are invisible**: the preconditions are asserts that release builds delete, and
the postcondition was never written at all. A future caller, or a future
refactor of `hd_ringbuf_reserve`, has nothing to check against.

This is bucket 2 in the [annotation-yield taxonomy](../../zstd/EXPERIMENT-annotation-yield.md):
bad spec, makes C clearer, may prevent a future bug.

## What it cost the toolchain

Closing it properly needs a precondition quantified over the ring's live slots
-- "for all i < len, buffer[(first + i) & mask] is readable" -- and the grammar
has no way to say that. Same gap as
[ISSUES.md#8](../../contracts-eval/ISSUES.md): clauses can name scalars, members
and contiguous slices, and cannot name a property of every element of a
collection. Two of the three real functions annotated so far have wanted it.

That is the strongest argument yet for a quantified clause, and it came from
trying to specify real code rather than from designing the language.
