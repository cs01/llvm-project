# lwrb in Safe C

[lwrb](https://github.com/MaJerle/lwrb) (3.4.0, d55fbde, MIT) is a ring buffer for embedded systems, used between interrupts and main
code (UART, DMA). `original/` is `lwrb.c` and `lwrb.h` as they were. `lwrb_ex.c` (`lwrb_overwrite`, `lwrb_move`) is built only with
`LWRB_DEV` and was not ported.

| | |
|---|---|
| Unchanged code under `-fsafe-c` | 30 errors in 1,022 lines |
| Port | no `__unsafe`, no `_Trusted` |
| Against the original (`run.sh`) | 300 runs of 3,000 random operations on two buffers: the same output |
| A planted off-by-one write | the port traps in 40 of 40 runs; the original writes past the buffer |

**What changed.** The struct keeps a pointer to a buffer it does not own, which Safe C does not store. On a device that buffer is a
global, so it is `uint8_t *_Static __counted_by(size)`: `lwrb_init` takes only an array that lives as long as the program, of the size
it says. The user's `arg` is `_Unchecked` (kept and handed back, never followed). Data is `uint8_t *` with its length instead of
`void *`. Three functions reused a length parameter as a counter; each has a local now. `memset` over the struct became `= (lwrb_t){0}`.

| You write | Safe C |
|---|---|
| `lwrb_init(&rx, rx_mem, sizeof rx_mem)`, both globals | accepted |
| `lwrb_init(&rx, local, sizeof local)` | error: the buffer must be a global |
| `lwrb_init(&rx, malloc(64), 64)` | error, the same |
| `lwrb_init(&rx, rx_mem, 100)` for a 64-byte array | error |
| `lwrb_read(&rx, out, 10)` into a 4-byte array | error |

**A crash in the original:** `lwrb_free` sets the buffer to `NULL` but keeps its size, and `lwrb_write_byte` and `lwrb_read_byte` (the
fast single-byte calls) do not check, so after `lwrb_free` they read or write through a null pointer (ASan: `SEGV`). `lwrb_write` and
`lwrb_read` check and return 0. Safe C made the port check too. Not found in lwrb's issues.

**A rule the port changed:** a global that holds pointers could not be passed to a function that changes it, because changing it might
free something. A struct whose pointers are all `_Static`, `_Unchecked` or functions frees nothing, so it may be now
(`clang/test/Frontend/safe-c-static-globals.c`).
