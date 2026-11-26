# heatshrink in Safe C

[heatshrink](https://github.com/atomicobject/heatshrink) (7d419e1, ISC) is LZSS compression for microcontrollers. Its decoder reads
whatever a device is sent. Ported in static mode (`HEATSHRINK_DYNAMIC_ALLOC 0`), as it is used on devices: buffers inside the struct,
no `malloc`. `original/` is the code as it was, with the same setting.

| | |
|---|---|
| Unchanged code under `-fsafe-c` | 4 errors in 971 lines, all one thing: the output buffer's length is not tied to its pointer |
| Port | `__counted_by` on the four buffer parameters and on the output struct, which became a `_View`; one local for a count that was reused. No `__unsafe`, no `_Trusted` |
| Against the original (`run.sh`) | 400 rounds: compress, decompress, and decompress damaged and random bytes, in random chunk sizes: 1,600 of 1,600 the same, every round trip gives the data back |
| Original under ASan and UBSan | no errors on the same input |
| Speed (1 MB, `-O2`) | compress 37 ms against 33; decompress 9 against 10 |
