# minmea in Safe C

[minmea](https://github.com/kosma/minmea) (c43c9e7, MIT/WTFPL) parses NMEA 0183 sentences from GPS receivers: bytes from a serial line,
often on a microcontroller. `original/` is the code as it was.

| | |
|---|---|
| Unchanged code under `-fsafe-c` | 32 errors in 681 lines |
| Port | no `__unsafe`, no `_Trusted`; every parser keeps its signature |
| Against the original (`run.sh`) | 50,000 generated and damaged sentences: the same output, except where the original overflows (below) |
| Original under ASan and UBSan, same input | no memory errors; two cases of undefined behaviour |

**What changed.** `minmea_scan` is gone. It was a `scanf` of its own: a format string chose which `...` pointer to write and as what
type, which nothing can check, and its `s` format copied a field into the caller's buffer with no size (none of minmea's parsers use
it, but code calling `minmea_scan` directly can overflow). In its place each field type has a function that reads the sentence by index
(`scan_f`, `scan_T`, ...), and each parser calls them in the order its format string listed. `strtol` with an end pointer became a
function that does the same on the sentence's bytes.

**Bugs in the original**, found by comparing the two:

| Input | What happens |
|---|---|
| a number with ten or more decimals, `4807.0000000001` | the scale overflows `int` (undefined behaviour); in practice the value's scale becomes garbage, so a position is wrong with no error. The port drops the extra digits, as minmea already does when the value itself is too long |
| ZDA hour offset `2147483648` | it becomes `INT_MIN` and `abs(INT_MIN)` is undefined: whether the sentence is accepted depends on the compiler. The port refuses it |

**What Safe C stopped at while porting:** `*sentence++ != '$'` on an empty string steps a string pointer past its terminator, which C
allows and Safe C does not; it is tested first now. `memset` over a `struct tm` (it holds a pointer) became `*tm = (struct tm){0}`.
`timegm` had no rule in the compiler, like `mktime`; it has now.

minmea's own tests are not run: most of them test `minmea_scan` itself, and they need the `check` library.
