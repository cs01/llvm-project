# puff under Safe C

Source: zlib `e3dc0a8`, `contrib/puff` (puff.c 840 lines, puff.h): Mark Adler's reference DEFLATE decoder, written to be read.
The pristine copy is the first commit of this directory, and `orig/` keeps it for the differential test.

Result: `puff_n`, the decoder, compiles under `-fsafe-c` (ownership on) with 0 errors and no `__unsafe` (24 errors before the
port). `puff()`, the original interface, is a small `_Trusted __unsafe` wrapper around it.

## What changed

| Change | Why |
|---|---|
| `setjmp`/`longjmp` on running out of input became a flag, `s->eof`, that `bits()` and `decode()` set; `puff_n` returns 2 when it is set, as the original did | `longjmp` goes back past checks |
| The Huffman tables are arrays in `struct huffman`, not pointers to arrays on the side | their sizes are then part of the type, and every index is checked |
| `struct state` is a `_View` with `__counted_by` on `in` and `out` | it borrows the caller's buffers |
| The match copy takes two `__counted_by(len)` slices, `to` and `from`, and copies between them | one check per copy instead of one per byte: the cost below went from 0.86x to 1.00x |
| `puff_n(dest, destlen, source, sourcelen)` takes the counts by value and returns `{err, outlen, inlen}` | a count read through a pointer could be changed through another name |

Lines: puff.c +70/-42 in all, with the wrapper and puff.h. The decoding loops are unchanged.

## Tests

`test/diff.c` (in `../test-ports.sh`, 20,000 cases there):

| Run | Result |
|---|---|
| 200,000 streams from zlib (every level, strategy and memory level), a third cut short, a third with bits flipped | the same return value, lengths and bytes as the original on every one; 58,659 checked against the input |
| The distance check (`dist > s->outcnt`, error -11) deleted from both | the original returns 0 on a corrupt stream and outputs bytes from before its buffer (ASan sees nothing: they belong to another global). Safe C traps at the read |
| The gate, with -11 changed to -12 in the port | fails on case 5 |

So puff's own checks were already right: the fuzzing found no difference. What Safe C adds is that a missing check is a trap, not a read of
other memory.

## Cost

`test/bench.c`, 13.8 MB of C++ (3.0 MB compressed), both builds by the same compiler at -O2, best of 7:

| Decoder | MB/s | vs original |
|---|---|---|
| original puff | 341 | 1.00x |
| Safe C puff, a check on every byte of the match copy | 289 to 295 | 0.85x to 0.86x |
| Safe C puff, the copy between two slices (this port) | 341 | 1.00x |
| the port built without Safe C (no checks) | 341 | 0.99x |
| zlib's inflate | 1,912 to 1,967 | 5.6x to 5.75x |

All of the 14% was the checks in one loop, and checking each copy's slices once took all of it back. With the distance check deleted,
the slice version still traps, at the slice.

puff is written to be read, not to be fast, and zlib's inflate is the one programs use.
