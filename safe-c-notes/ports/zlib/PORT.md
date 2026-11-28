# zlib's inflate_fast under Safe C

Source: zlib `e3dc0a8` (the inflate sources, and deflate's for the test). Only `inffast.c` is ported: `inflate_fast()`, the loop that
decodes almost every byte zlib inflates, and where its bounds come from how much input and output is left rather than from checks.
`inflate.c` and the rest are plain C. `orig/` keeps the pristine `inffast.c`.

Result: `inffast.c` compiles under `-fsafe-c` (ownership off: zlib's headers are not ported) with every access checked, two
`__unsafe` blocks where it meets `inflate()`, and matches stock zlib's speed.

## What changed (+59/-44)

| Change | Why |
|---|---|
| On entry, one `__unsafe` block gives the locals bounds: the input (`avail_in`), the output from where this `inflate()` call began (`start`), the window (`wsize`), the code tables (the most `inftrees.c` builds) | `inflate()` is not ported; this is where it vouches for its buffers. Every access after it is checked |
| On exit, one `__unsafe` block stores `next_in` and `next_out` back | `next_out` may be one past the end of the buffer, which a plain pointer field cannot hold |
| Every match copy (6 loops: from the window, around its end, from the output) is one call to `copy_bytes(to, from, n)`, whose parameters are `__counted_by(n)` | each copy is checked once, not once per byte |
| `window` in `inflate.h` is `_Nullable` | it is null until `inflate()` first needs it; Safe C traps on loading a null pointer from a field not marked so |

## Tests

`test/run.sh` (in `../test-ports.sh`, 5,000 cases there) builds stock zlib with `Z_PREFIX` and the port into one program:

| Run | Result |
|---|---|
| 20,000 streams from zlib (raw, zlib and gzip wrappers, every level and strategy, small windows), a third cut short, a third with bits flipped, read out in chunks of 1 to 4,096 bytes so copies cross the window | every return code and byte the same as stock zlib |
| zlib's "invalid distance too far back" check deleted from the port | traps at the copy |

## Cost

`test/run.sh bench FILE`, 13.8 MB of C++, both built by the same compiler at -O2, best of 7:

| Build of inffast.c | Whole buffer | 16 KB chunks |
|---|---|---|
| stock | 621 MB/s | 547 MB/s |
| Safe C, a check on every access (the loops as written) | 0.75x | 0.77x |
| Safe C, each copy checked once (this port) | 0.98x | 0.97x |
| Safe C, `__unsafe _No_bounds_checks` (no checks at all) | 1.01x | 1.00x |

Three quarters of the cost was the per-byte checks in the copy loops. What is left, 2% to 3%, is the input reads and table lookups,
a few per decoded symbol. (Apple's system libz is about three times faster than either: it is a different, vectorized inflate.)
