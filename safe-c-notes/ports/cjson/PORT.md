# cJSON under Safe C

Source: DaveGamble/cJSON 6d9f244 (3,206-line cJSON.c plus its unity tests). The
pristine copy is the first commit of this directory; `git log -p -- .` shows the
port. Ported by an agent, checked by claude: cJSON.c compiles with 0 errors
under `-fsafe-c`, and 18 of 18 test programs pass under Safe C and plain clang
at -O2. The 3 cJSON_Utils tests are not ported (that library is not included).
unity.c, the test harness, is compiled as plain C.

## Cost

- cJSON.c and cJSON.h: 448 changed lines out of 3,512. Most are annotations:
  268 `_Nullable`, 28 `__null_terminated`, 17 `__counted_by_or_null`,
  16 `__unsafe_indexable`, 9 `__bidi_indexable`, 7 `__sized_by_or_null`,
  2 `__counted_by`.
- 3 `__unsafe` blocks, all in the allocator hooks: giving a `void *` from
  `global_hooks.allocate` a type (`allocate_bytes`, `reallocate_bytes`,
  `allocate_item`). Every allocation goes through these three helpers.
- `sprintf`/`strcpy` into counted buffers became `snprintf`/`memcpy`.
- `strncmp` on input slices became `memcmp` (same gap as jsmn).

Compared with jsmn (4 annotated signatures), cJSON costs more because it is
a tree of nullable pointers, owns strings, and allocates through hooks. About
60% of the annotations are `_Nullable`, which mostly records what the code
already handled.

## Bugs found (tests, not the library)

- `tests/print_number.c:39`: the buffer points at a 26-byte array while its
  length says 1024.
- `tests/minify_tests.c:146`: `malloc` result is not checked.
- `tests/misc_tests.c`: about 20 unchecked `cJSON_Create*` results.

## Compiler gaps it found, by priority

Soundness:
1. `strcpy`/`sprintf` into a `__counted_by(n)` buffer compiles and overflows.
2. `__unsafe_null_terminated_from_indexable`/`_to_indexable` are accepted
   outside `__unsafe`.

Usability (Safe C rejects or traps on safe code):
3. A cast of a `__null_terminated` pointer to another character type gives
   `__single`, and then `u[1]` traps.
4. Allocator hooks: a function pointer with `malloc`/`alloc_size` should count
   as a fresh allocation. This is the only reason for the 3 `__unsafe` blocks.
5. Null checks on a `__counted_by_or_null` field do not narrow it.
6. Predicates (`cJSON_IsString(n) && n->valuestring`) need a way to say "true
   means the argument is nonnull".
7. Passing a `__null_terminated` pointer to `free(void *)` asks for a
   terminator scan.
8. Locals do not take `__null_terminated` from their initializer.
9. A view that includes the terminator needs an `__unsafe_` spelling.
10. endptr idioms (`strtod`, `return_parse_end`) have no thin type for "a
    pointer into the caller's buffer".
11. The definition must repeat every bounds annotation from the header.
12. Stack frames are about 16 times larger at -O0 (1,840 vs 112 bytes in
    `cJSON_Duplicate_rec`), so deep recursion overflows. At -O2 the frame is
    64 bytes.
13. `strncmp` on a slice (also found in jsmn).
