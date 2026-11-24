# lz4 under Safe C

Source: lz4/lz4 `dev` at 94171b0 (v1.11.x): lib/lz4.c (3,014 lines) and lib/lz4.h
(897 lines). The pristine copy is the first commit of this directory;
`git log -p -- .` shows the port. lz4hc and lz4frame are not in scope.

Result: lz4.c compiles with 0 errors under `-fsafe-c` (226 before the port), and
`test.c` passes under Safe C -O2 (with and without `-mllvm -safe-c-ownership`),
Apple clang -O2, and Apple clang with ASan+UBSan. `../test-ports.sh` runs it. The
compressed output is byte-identical to upstream's.

## Cost

Lines: lz4.c +412/-694, lz4.h +43/-72 (against 3,911). About 570 of the deleted
lines are the removed APIs below; the rest is the rewritten history handling.

Annotations (lz4.c + lz4.h; a public function's annotations appear twice, in the
header and the definition): 89 `__counted_by`, 8 `__sized_by`, 68 `__bidi_indexable`
(parameters of internal helpers that do pointer arithmetic), 62 `_Nullable`,
4 `_Unchecked __unsafe_indexable` (the stream fields), 4 `_Owned`,
4 `_Returns_owned`, 4 `_Lifetimebound`.

The public API keeps its names and parameters; the annotations say what the
documentation already said (`src` has `srcSize` bytes, `dst` has `dstCapacity`).
`*srcSizePtr` is the count of `src` in the destSize and incomplete functions.

Removed (deprecated or obsolete upstream):
- `LZ4_decompress_fast`, `_fast_continue`, `_fast_usingDict` (no input bound;
  documented as unsafe on malformed input) and their decoder
  `LZ4_decompress_unsafe_generic`.
- The obsolete exports removed from lz4.h in v1.11.0: `LZ4_compress`,
  `LZ4_compress_limitedOutput`, `_withState`, `_continue`, `LZ4_uncompress*`,
  `LZ4_create`, `LZ4_sizeofStreamState`, `LZ4_resetStreamState`,
  `LZ4_slideInputBuffer`, `LZ4_decompress_*_withPrefix64k`.
- `LZ4_resetStream` (deprecated), `LZ4_compress_forceExtDict` (a hidden test
  entry point that writes up to `LZ4_compressBound(srcSize)` into a dst of unknown
  size).
- The byPtr hash table mode (32-bit targets only; the table held pointers): 32-bit
  targets use byU32. The `LZ4_FORCE_MEMORY_ACCESS` 1 and 2 variants (packed structs,
  direct unaligned dereference): memcpy into an integer is the only one.

Other changes:
- Helpers take `BYTE*` with bounds instead of `void*` (a byte view of `void*` is
  rejected: it could forge a pointer stored there).
- `LZ4_stream_t` and `LZ4_streamDecode_t` lost their `char minStateSize[]` union
  member (gap 2). Their size is unchanged on 64-bit targets (static asserts).
- The history (the stream's unchecked pointers) becomes bounded pointers once per
  block in `LZ4_compress_generic()` and `LZ4_decompress_safe_continue()`, and is
  passed down as parameters.
- `LZ4_resume_t` stores offsets instead of pointers (gap 1).
- `LZ4_decompress_safe_usingDict`, `_partial_usingDict` and `LZ4_decompress_incomplete`
  decode a dictionary that ends at `dst` as an external dictionary instead of a
  prefix: the same output, without a pointer spanning two buffers (gap 3).
- Asserts that guarded memory safety became checks: a misaligned state makes
  `_extState` return 0, a NULL `src` with a size returns 0, `saveDict` with a NULL
  buffer saves nothing.
- A negative size passed for a `__counted_by` parameter traps at the call under
  Safe C (upstream returns an error). Plain C builds behave as upstream.

## __unsafe: 14 blocks, all in lz4.c, each with a one-line reason

- 9 for the streaming API's history (gap 1): the stream keeps borrows of the
  caller's previous block, loaded dictionary, or attached stream across calls,
  which the API requires to stay alive. Each block forges a bounded pointer from an
  unchecked field and its size (`LZ4_compress_generic` x4, the dictCtx copy in
  `LZ4_compress_fast_continue`, `LZ4_saveDict`, `LZ4_decompress_safe_continue` x3).
  Two of these also join the prefix with the current buffer after checking they
  are adjacent (gap 3).
- 2 casts of `void* state` to `LZ4_stream_t*` (gap 2): `LZ4_initStream` (after
  checking size and alignment and zeroing the buffer) and
  `LZ4_compress_fast_extState_fastReset` (the bytes are read as integers through
  bounds-checked pointers; the two pointers are cleared before use).
- 3 that read an unchecked pointer's address as an integer, or move it within
  its history (gap 4).

No `__unsafe` silences a check in the compressor or decompressor loops: those are
checked code.

## Tests (`test.c`)

Round trip of (a) lz4.c + lz4.h (169,827 bytes), (b) 1 MB of random bytes,
(c) 1 MB of a repeated pattern, (d) empty input. For each: a dst one byte short is
rejected, inputs truncated by 1 to 16 bytes are rejected, 2,000 single-byte
corruptions decode without a crash and never past dst, and partial decoding works.
Also streaming (64 KB blocks with `_continue` both ways, a loaded dictionary decoded
with `usingDict` both separate and as a prefix) and `LZ4_compress_destSize`.

```
safec: PASS (all passed: 0 failure(s))
apple: PASS (all passed: 0 failure(s))
asan: PASS (all passed: 0 failure(s))
```

A mutation fuzz of the three decoders (300,000 inputs, 1 to 4 corrupted bytes,
random truncation and capacity) under Safe C -O2: no trap. A caller that lies about
`dstCapacity` (16-byte buffer, says 8192) traps at the call under Safe C.

## Speed and size (Apple M-series, -O2, 20 MB of C sources from safec-census, best of 5, 3 runs)

| build | compress MB/s | decompress MB/s | lz4.o __TEXT |
|---|---|---|---|
| upstream lz4.c, Apple clang | 1004 to 1028 | 6033 to 6250 | 59,975 |
| port, Apple clang | 989 to 1017 | 6033 to 6202 | 47,155 |
| port, the fork without -fsafe-c | 997 to 1023 | 6131 to 6289 | 50,015 |
| port, Safe C | 770 to 786 | 3469 to 3620 | 82,654 |

Safe C runs compression at 0.77x and decompression at 0.57x of Apple clang, with
1.75x the code. The fork without `-fsafe-c` matches Apple clang, so the cost is the
bounds checks, not the compiler. The decoder copies fixed 16, 32 and 64-byte stripes
guarded by its own `oend - op >= 64` loop condition; each copy still checks both
pointers against both bounds. The plain C port is smaller than upstream because
the deprecated decoder and byPtr mode are gone.

## Compiler gaps, by how much fixing them would help

1. **No struct with a lifetime behind a pointer** (9 `__unsafe`, and the resume
   state rewritten as offsets). A stream keeps borrows across calls, like Rust's
   `struct Stream<'a> { dict: &'a [u8] }`. `_View` structs live only in locals,
   and even a `_View` out-parameter cannot be filled in:
   ```c
   struct _View V { const BYTE *__bidi_indexable ip; };
   void f(const BYTE *__counted_by(n) s, size_t n, struct V *out) { out->ip = s + n / 2; }
   // error: writing a borrow into a '_View' through a pointer can outlive what it points to
   ```
2. **A struct whose only pointers are `_Unchecked` counts as pointer-holding**
   (2 `__unsafe`, the union member removed, typed internal entry points). Rust
   treats raw pointers as plain data (making one from an integer is safe;
   dereferencing it is not), so writing bytes over one cannot break safety.
   Here a size-fixing union, passing the struct as `void*` state, and casting a
   zeroed `void*` buffer back are all rejected:
   ```c
   struct S { uint32_t t[4]; const BYTE *_Unchecked _Nullable d; };
   union U { char size[64]; struct S s; };
   int f(union U *u) { return (int)u->s.t[0]; }  // error: ... can read memory as a pointer
   ```
   The same struct passed to `void *__sized_by(64)` gives "reading bytes into
   'struct S', which holds pointers, can forge a pointer"; `memset` then
   `(struct S *)buf` gives "can treat memory as the wrong type".
3. **An unchecked pointer's address and arithmetic need `__unsafe`** (3 blocks),
   though comparing it to NULL does not. Rust's `p as usize` and `p.wrapping_add(n)`
   are safe on raw pointers.
   ```c
   struct S9 { const BYTE *_Unchecked _Nullable __unsafe_indexable d; };
   uintptr_t addr(const struct S9 *s) { return (uintptr_t)s->d; }   // error: 'd' is '_Unchecked'
   void bump(struct S9 *s) { s->d = s->d + 1; }                      // error: 'd' is '_Unchecked'
   ```
4. **Two bounded buffers proven adjacent cannot be joined** (inside 2 of the
   gap 1 blocks; usingDict and incomplete lost upstream's faster prefix path). Rust
   has the same hole (`slice::from_raw_parts`), so this is low priority.
   ```c
   int dec(const char *__counted_by(d) dict, int d, char *__counted_by(n) dst, int n) {
     if (dict + d == dst) { /* want one pointer over [dict, dst + n): no safe spelling */ }
   ```
5. Small:
   - Nullability that depends on a mode flag (`dictStart` is read only when
     `dict == usingExtDict`) has no spelling. A 1-byte static array stands in for
     NULL. `inputConsumed` and `srcEndPtr` got null checks.
   - The count pointer of a `__counted_by(*n)` parameter cannot be passed on as
     an `int*`. A local copy works:
     `void g(const char *__counted_by(*n) s, int *n) { consume(n); }  // error`
   - Definitions must repeat every bounds annotation of the header (also in cJSON).
   - Bounds checks the program already guards against are kept (see Speed). That
     is a cost, not a rejection, but it is the largest one.
