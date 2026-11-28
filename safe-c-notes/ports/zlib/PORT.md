# zlib's inflate under Safe C

Source: zlib `e3dc0a8` (the inflate sources, and deflate's for the test). Ported: `inflate.c` (the state machine and the API),
`inftrees.c` (code table building) and `inffast.c` (`inflate_fast()`, the loop that decodes almost every byte). They compile
under `-fsafe-c` with ownership off (`-mllvm -safe-c-ownership=false`: zlib's headers are not ported for ownership). `adler32.c`,
`crc32.c` and `zutil.c` are plain C. `orig/` keeps the pristine `inflate.c`, `inftrees.c`, `inffast.c` and their headers, for the
stock build the tests compare against.

Result: every buffer access in the three files is checked against a size that travels with the pointer. What is trusted is
five small `__unsafe` blocks, each one where zlib's API hands over memory whose type or size the code cannot see, and one
header of declarations for the plain C files. Speed is 0.96x to 0.99x stock.

## Where the sizes come from

| Pointer | Size | Where it is declared |
|---|---|---|
| `strm->next_in`, `strm->next_out` | `avail_in`, `avail_out` | `zlib.h`, `__counted_by` |
| `head->extra`, `head->name`, `head->comment` | `extra_max`, `name_max`, `comm_max` | `zlib.h`, `__counted_by_or_null` |
| `inflateSetDictionary()`'s dictionary | `dictLength` | `zlib.h` |
| `adler32()`, `crc32()`'s buffer | `len` | `zunported.h` (and `zlib.h`) |
| `state->window` | `walloc`, new: bytes allocated | `inflate.h` |
| `state->lencode`, `state->distcode` | `lensize`, `distsize` | `inflate.h` |
| `state->next` (where the next table is built) | the bounds of `codes[]` | `inflate.h`, `__bidi_indexable` |
| `inflate_table()`'s `lens`, `work` | `codes` | `inftrees.h` |
| `inflate_fast()`'s `beg` (output start of this `inflate()` call) | `start` | `inffast.h`, new parameter |

`zlib.h` is also read by plain C callers. It includes the vendored `safec.h`, which defines the marks as nothing for other
compilers. The `gz_header` counts are the ones used when reading a header; deflate, which writes one from `extra_len`, is not
built under Safe C.

## What changed

| Change | Why |
|---|---|
| One function, `inflate_state_of()` in `inflate.h`, turns `strm->state` into a `struct inflate_state *` | `zlib.h` declares `state` as a pointer to `struct internal_state`, which is never defined. There were 20 casts; now there is one |
| `inflateStateCheck()` tests `strm->state` for null before that call, then `state->strm == strm` as before | the same checks in the same order, with the null test where the cast cannot trap on it |
| Allocation goes through `zalloc_bytes()` (window) and `zalloc_state()`; frees through `zfree_bytes()`, `zfree_state()` | zalloc returns a `void *` of a size Safe C cannot see. These give it that size, once. `zalloc_state()` zeroes the state with a struct assignment, not `memset` on a `void *` |
| Pointer fields and their counts are set together (`lencode` and `lensize`, `window` and `walloc`, `head->extra` and `extra_max`, ...) | a counted pointer is checked against its count when it is stored. Where zlib sets a header pointer to null and leaves its count alone, the count is stored again unchanged |
| `inflate()` keeps `put`'s bounds from the start of the call: after `inflate_fast()` it moves `put` on by what was written instead of reloading it from `strm->next_out` | the check value (`CHECK`, `inf_leave`) and `updatewindow()` read back over this call's output. A pointer reloaded from `next_out` only knows the space ahead of it |
| `updatewindow(strm, buf, n)` takes the n bytes, not the pointer to their end | a pointer to the end has no room before it |
| `inflate_fast(strm, start, beg)` gets the call's output start as a parameter | the same reason: match copies reach back up to `start` bytes from `next_out`. Before, inffast.c forged this range |
| `next_in` may be null with `avail_in` 0 (zlib.h allows it). Inside `inflate()` the local `next` is then `no_input`, a one-byte static array, and `RESTORE()` stores the null back | Safe C rejects arithmetic on a pointer that may be null. `have` is 0, so `no_input` is never read; the caller sees the same `next_in` as with stock zlib |
| `window_of(state)`: the window, or a one-byte static array before it is allocated | the window is null until first used, and whave is 0 then, so nothing reads it. Readers need no null test, and a read that went wrong is out of bounds and traps |
| `inflateCopy()` copies `z_stream` and the state with struct assignments, not `zmemcpy` | a struct assignment keeps each pointer with its bounds |
| `inflate()`'s match copy is one call to `copy_bytes()` (moved from inffast.c to inflate.h) | checked once per match, not once per byte |
| `inflate_table()`: in the `CODES` case `base` and `extra` point at `work` (never read), and the `DISTS` case is the `default` | so every path sets them non-null, as zlib 1.2 had it |
| `inflateSync()` searches only if `next_in` is not null | a null `next_in` has `avail_in` 0, so there is nothing to search; stock zlib's result is the same |
| `zalloc`, `zfree`, `opaque`, `msg`, `state`, `next_in`, `next_out`, `head`, the header pointers: `_Nullable` | each may be null. Safe C treats an unmarked pointer field as never null and traps on loading a null from it |
| `zunported.h` (marked `#pragma clang safe_c legacy`) declares `adler32()`, `crc32()`, `zcalloc()`, `zcfree()` first | Safe C checks at link time that a function a Safe C file calls was compiled as Safe C. These are not ported, so they are declared as trusted here. Their callers are still checked: a buffer passed to `adler32()` must hold `len` bytes |
| inffast.c's entry block is gone; its exit stores each pointer with its count | everything it forged now arrives with a size |

## Trusted code

| Site | What it vouches for |
|---|---|
| `inflate_state_of()` (`inflate.h`) | `strm->state` was set by `inflateInit2_()` or `inflateCopy()`. Callers have passed `inflateStateCheck()` |
| `set_state()` (`inflate.c`) | the reverse cast, `struct inflate_state *` to `struct internal_state *`, so that `inflate_state_of()` gets back what was stored |
| `zalloc_bytes()` | the application's `zalloc` returned `items` bytes (zlib.h's contract for it) |
| `zalloc_state()` | the same, for `sizeof(struct inflate_state)` bytes, zeroed before use |
| `zfree_state()` | passing the state to `zfree` as a `void *`: it came from `zalloc_state()` and is not used after |
| `inflateGetDictionary()` | the caller's `dictionary` holds `whave` bytes. zlib.h asks for that (32768 is always enough) and has no way to be told its size |
| `zunported.h` | `adler32()`, `crc32()` (crc32.c, adler32.c) and `zcalloc()`, `zcfree()` (zutil.c), compiled as plain C, do what their declarations say |

Everything else in `inflate.c`, `inftrees.c` and `inffast.c` is checked. The pointer the application gives in `next_in` and
`next_out` is believed to hold `avail_in` and `avail_out` bytes: that is zlib's API, and a plain C caller is not checked.

## Tests

`test/run.sh` (in `../test-ports.sh`, 5,000 cases there) builds stock zlib with `Z_PREFIX` from `orig/` and the port into one
program. `SEED` picks the fuzz seed.

| Run | Result |
|---|---|
| 20,000 streams from zlib (raw, zlib and gzip wrappers, every level and strategy, small windows), a third cut short, a third with bits flipped, read out in chunks of 1 to 4,096 bytes so copies cross the window; seeds 1, 2 and 3 | every return code and byte the same as stock zlib |
| `test/api.c`: a gzip header read into 10-, 5- and 7-byte buffers, preset dictionaries (zlib and raw, a wrong one first, read back), `inflateCopy()` mid-stream, `inflateSync()` past a damaged block | the same as stock zlib. Each part fails when the port is broken there (a name bound off by one, a dictionary read back one byte short, a window half copied, `total_in` off by one) |
| `test/cve.c`, CVE-2022-37434: a 200-byte gzip extra field, 1 to 4 bytes per call, into a 16-byte `extra` | decoded, the first 16 bytes kept |
| The same, with zlib's fix for it (`< extra_max`) deleted from the port | traps at the copy (exit 133, "Bounds check failed") |
| The same, with the fix deleted from stock zlib, built with ASan | ASan stops the copy: the length is `extra_max - len` wrapped around, 4,294,967,294 bytes |
| inflate.c's "invalid distance too far back" check deleted | traps |
| inffast.c's "invalid distance too far back" check deleted | traps |
| inflate.c's check that a code length repeat fits (`have + copy > nlen + ndist`) deleted | traps |
| The literal/length table's size given one short | traps |
| `updatewindow()` copying the rest of the window regardless of how much output there was | traps |
| The `LIT` state writing without testing for output space | traps |

The mutants are made on a copy, built, and run on the 20,000 streams of seed 1.

## Cost

`test/run.sh bench FILE`, 13.8 MB of C++, both built by the same compiler at -O2, best of 7, three runs:

| Build | Whole buffer | 16 KB chunks |
|---|---|---|
| stock | 608 to 620 MB/s | 545 to 548 MB/s |
| only inffast.c under Safe C (before this change) | 0.99x | 0.98x |
| inflate.c, inftrees.c, inffast.c under Safe C (this port) | 0.98x to 0.99x | 0.96x to 0.97x |
| the same, with `inflate()` itself `_No_bounds_checks` (measurement only) | 0.98x to 0.99x | 0.97x |

With whole buffers nearly all bytes go through `inflate_fast()`, whose cost is unchanged. With 16 KB chunks the last 258 bytes
of each chunk are decoded in `inflate()` itself, one symbol at a time, and the window is copied each call; that part now costs
about 1%. Checking each match once (`copy_bytes()`) instead of each byte took 16 KB chunks from 0.95x to 0.96x or 0.97x. Earlier history
of inffast.c: three quarters of its first cost was per-byte checks in the copy loops (0.75x before `copy_bytes()`). Apple's
system libz is about three times faster than either: it is a different, vectorized inflate.
