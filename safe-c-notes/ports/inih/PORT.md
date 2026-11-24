# inih under Safe C

Source: benhoyt/inih r63 (as vendored in safec-census): ini.c (334 lines), ini.h
(203 lines) and the four test programs in tests/. The pristine copy is the first
commit of this directory; `git log -p -- .` shows the port. examples/, fuzzing/ and
the C++ wrapper are not in scope.

Result: ini.c compiles with 0 errors under `-fsafe-c` (26 before the port) in all 16
build configurations upstream's `tests/unittest.sh` uses. All 16 test builds print
exactly upstream's checked-in baselines under Safe C (default and with
`-mllvm -safe-c-ownership=true`), Apple clang, and Apple clang with ASan+UBSan.
`../test-ports.sh` runs them.

## Cost

Lines: ini.c +101/-69, ini.h +37/-11, tests +58/-28.

Annotations in ini.c and ini.h: 9 `__bidi_indexable`, 5 `__counted_by`,
1 `__counted_by_or_null`, 2 `__sized_by_or_null`, 7 `_Noescape`, about 23 `_Nullable`,
3 `_Lifetimebound`, 2 `_Owned`, 2 `_Returns_owned`, 2 `alloc_size`, 1 `_View`.

## What changed

- **No `void *` stream inside ini.c.** Upstream routes every source through
  `ini_parse_stream(reader, void *stream)`: `ini_parse_file` passes `(ini_reader)fgets`
  with the `FILE *`, and `ini_parse_string` passes `ini_reader_string` with a
  pointer to a `{ptr, num_left}` struct. Safe C rejects each step (the function
  pointer cast, passing a `FILE *` or a pointer-holding struct as `void *`, and the
  cast back; gaps 1 and 2). The parser is now `ini_parse_source()` over an
  `ini_source`, a `_View` struct whose `kind` says whether to call the user's reader,
  `fgets` on a `FILE *`, or copy from a counted string. `ini_reader_string` became
  the string case of `ini_read_line()` and keeps an offset instead of moving a
  pointer. The public API keeps its names and parameters; only a user's own
  reader still receives a `void *`.
- **Handler strings are `_Noescape`.** `section`, `name` and `value` point into the
  parser's stack buffers, which it rewrites after the call; the header already said
  "data only valid for duration of handler call". With `_Noescape` in the
  `ini_handler` typedef, passing those buffers as terminated strings is accepted, and
  a handler that stores one is a compile error. Handlers must be declared with
  `_Noescape` too (a handler without it does not convert). The header's
  permission to cast `value` to `char *` and write it no longer holds: Safe C
  rejects casting away `const`.
- **Nullability follows the build options.** `name` is `_Nullable` only with
  `INI_CALL_HANDLER_ON_NEW_SECTION`, `value` only with that or `INI_ALLOW_NO_VALUE`
  (macros `INI_NAME_NULLABLE`, `INI_VALUE_NULLABLE`), so a default build's handlers
  need no NULL checks, and a build where NULL can arrive forces them. `user` is
  `_Nullable` everywhere (unittest_alloc passes NULL).
- **The string helpers take `__bidi_indexable` pointers** (`ini_rstrip`,
  `ini_lskip`, `ini_find_chars_or_comment`, `ini_strncpy0`'s source) instead of
  `const char *`, which Safe C reads as `__null_terminated`. Their results point into
  the line buffer that the parser keeps writing, so a terminated pointer would be
  rejected (gap 5); a bounded pointer is checked on every access instead. They return
  `char *` without upstream's const cast. `ini_strncpy0`'s destination is
  `__counted_by(size)`.
- `ini_parse_string_length` takes `__counted_by(length)`; `ini_parse_string` converts
  with `__null_terminated_to_indexable`.
- The `reader != NULL`, `stream != NULL` and `handler != NULL` asserts are gone: the
  parameters are non-null, which Safe C checks at every call (plain C builds lose
  the debug assert).
- `ini_reader` is now `char *_Nullable (*)(char *__counted_by(num), int num, void *)`.
- The custom allocator declarations gained `alloc_size`, `_Returns_owned`,
  `__sized_by_or_null` and `_Owned`, so the `void *` result may be cast to `char *`
  (Safe C checks the definition returns fresh memory) and the line buffer is
  bounded.

Tests: the handlers gained `_Noescape` and the nullability macros; `strncpy` (unavailable
under bounds safety) became `strlcpy`, which gives the same result here; the mutable
globals are `_Thread_local` (Safe C rejects an unsynchronized mutable global).
unittest_alloc.c's allocators repeat ini.c's annotations.

## __unsafe: 1 in ini.c, 2 in the tests

- ini.c, the `INI_ALLOW_REALLOC` growth loop: `ini_realloc` and the `ini_free` on
  its failure path (gaps 3 and 4). realloc keeps the old block when it fails, so
  freeing it then is correct; Safe C has no way to say so. `new_line` still gets
  its bounds from `ini_realloc`'s `__sized_by_or_null(size)`.
- unittest.c and unittest_string.c, `user_value()`: the cast of the handler's
  `void *user` back to the `int *` the test passed (gap 2). This is test code; the
  library never casts `user`.

## Tests

`../test-ports.sh` builds ini.c with each test program in the 16 configurations of
upstream's `tests/unittest.sh` (multi-line, max line 20, single-line, no inline
comments, stop on first error, handler line numbers, string input, heap buffer,
heap with realloc, custom allocator, call handler on new section, no-value lines,
file read error), runs each in tests/, and requires exit 0 and output identical to
the baseline_*.txt upstream checked in.

```
inih safec: 16/16
inih safec-own: 16/16
inih system: 16/16
inih asan: 16/16
```

The other ports, in the same run: cjson 18/18, jsmn 1/1, lz4 1/1, std 2/2 in all four
configurations. The gate fails when it should: a planted off-by-one in
`ini_strncpy0` made 9 of 16 fail in every configuration.

`test-ports.sh` itself needed one fix: commit 2ae8be84 made the driver reject a bare
`-mllvm -safe-c-ownership` (only `=true` or `=1` are allowed for `-safe-c-*`
switches), so the safec-own configuration failed to build every port. It now
passes `-mllvm -safe-c-ownership=true`. Ownership is on by default, so safec and
safec-own now test the same thing.

## Compiler gaps, by how much fixing them would help

1. **Soundness: a pointer moved out of a field with `_Take` is not tracked.** Use
   after free and double free of it compile with no error. This came up while
   working around gap 3: a reallocf helper over a struct field was accepted without
   `__unsafe`, which looked wrong. This port does not rely on it.
   ```c
   struct buf { char *_Owned _Nullable p; };
   void twice(struct buf *_Exclusive b) {
     char *_Nullable old = _Take(b->p);
     free(old);
     free(old);            /* accepted; at run time libmalloc traps */
   }
   int uaf(struct buf *_Exclusive b) {
     char *old = _Take(b->p);
     if (!old) return 0;
     free(old);
     return old[0];        /* accepted */
   }
   ```
   The same code on an `_Owned` parameter is rejected ("parameter 'old' does not
   live long enough"), as it should be.

2. **Casting a function to a typedef of its own type is rejected, twice.** This is
   the real error behind `(ini_reader)fgets`. A function declaration's return type
   carries an implicit `__single`, while the typedef's does not, so the cast compares
   two types that differ only in that sugar, and both directions are reported. No
   `_Nullable` or `FILE` is involved:
   ```c
   typedef char *(*reader2)(char *s, int n);
   char *rd2(char *s, int n);
   reader2 get3(void) { return (reader2)rd2; }
   // error: converting 'char *__single(*__single)(char *__single, int)' to 'reader2'
   //   (aka 'char *(*)(char *__single, int)') can treat memory as the wrong type
   // error: converting 'char *(*__single)(char *__single, int)' to
   //   'char *__single(*__single)(char *__single, int)' can treat memory as the wrong type
   // note: a pointer cast is allowed to 'void *', to the same type, ...
   ```
   The same code without the cast (`return rd2;`) is accepted. Casting a callback to
   its typedef is very common C.

3. **A `void *` context passed through to a callback has no safe spelling, and the
   caller's message is wrong.** This is the known candidate: "reading bytes into
   'FILE'" is not about the fgets cast. It is reported at the `fp` argument, because
   any non-const `void *` parameter is treated like `read()`'s buffer. Nothing reads
   bytes here, and the suggested fix (read into a pointer-free buffer) does not apply:
   ```c
   int run(int (*cb)(void *ctx), void *ctx);
   int cb(void *ctx);
   int f(FILE *fp) { return run(cb, fp); }
   // error: reading bytes into 'FILE' (aka 'struct __sFILE'), which holds pointers,
   //   can forge a pointer; read into a pointer-free buffer and set the pointers from it
   struct _View ctx { const char *p; };
   int g(struct ctx *c) { return run(cb, c); }   // the same error for 'struct ctx'
   ```
   The callee side is rejected too (`(FILE *)stream`: "converting 'void *__single' to
   'FILE *' can treat memory as the wrong type", with a note suggesting
   `__attribute__((callback(...)))`, which Safe C's checks do not read). That is open
   item 11 in review-2026-11-23.md. It cost this port a restructured `ini.c`, and the
   tests 2 `__unsafe`. Rust would use a generic `R: Read` or `&mut dyn Read`.
   A typed context (Safe C checking that the `void *` given to `run` is the one `cb`
   gets, through `callback(cb, ctx)`) would cover inih, qsort_r, pthread_create and
   most C callback APIs.

4. **An allocation inside a loop is treated as one object across iterations.** Freeing
   last iteration's block is reported as freeing this one. This is correct code
   (and every growable buffer looks like it):
   ```c
   for (int i = 0; i < n; i++) {
     char *q = malloc(8);
     if (!q) break;
     free(p);
     p = q;
     p[0] = 'a';
   }
   // error: object allocated by 'malloc' does not live long enough
   // note: object allocated by 'malloc' is freed here      (free(p))
   // note: local variable 'q' aliases the storage of object allocated by 'malloc'
   // note: later used here                                 (p[0] = 'a')
   ```
   Also with `p = realloc(p, n)` in a loop, and with a `_Returns_owned` helper called
   in a loop. Rust has no such limit: each `Box::new` in a loop is a new value.

5. **realloc's failure path cannot be written** (cJSON gap 4, confirmed here). When
   realloc returns NULL the old block is still valid and must be freed, but Safe C
   treats realloc as freeing its argument on every path:
   ```c
   char *p = malloc(4);
   if (!p) return -1;
   char *q = realloc(p, 8);
   if (!q) { free(p); return -1; }
   // error: object allocated by 'malloc' does not live long enough
   // note: object allocated by 'malloc' is freed here   (realloc)
   // note: later used here                              (free(p))
   ```
   A reallocf-style helper (frees on failure) does not help: its own body has the
   same error ("parameter 'p' does not live long enough"). Modelling realloc as
   "takes p only when it returns non-NULL" would remove the ini.c `__unsafe` (together
   with gap 4).

6. **The terminated-string escape message suggests fixes that cannot work for a
   function that returns into its argument** (the second candidate, confirmed as a
   correct rejection with a misleading message). `ini_lskip(line)` returns a pointer
   into `line`, and the parser then writes to `line`, which could erase the
   terminator that pointer scans for. So the rule is right. But both suggested
   fixes fail: `noescape` gives "parameter is marked [[clang::noescape]] but escapes"
   because the function returns it, and strdup changes the function. The fix that
   works, a bounded parameter (`char *__bidi_indexable`), is not suggested:
   ```c
   static const char *lskip(const char *s) {
     while (*s && isspace((unsigned char)*s)) s++;
     return s;
   }
   int f(void) { char line[16] = "  a=b"; const char *p = lskip(line); line[5] = 'x'; return *p; }
   // error: a buffer passed as a terminated string is checked for its terminator only
   //   for the call, but 's' may keep the pointer, and a later write to the buffer could
   //   erase the terminator; mark 's' 'noescape' if the function does not keep it, or
   //   pass a string the callee can own (strdup)
   ```

7. Small:
   - A handler without `_Noescape` passed as an `ini_handler` gives the correct
     "incompatible function pointer types" error, and also a second
     "can treat memory as the wrong type" with the note about `void *` and memcpy,
     which is not the problem (`int plain(void *u, const char *s); handler h = plain;`
     where the typedef has `const char *s _Noescape`). The type printer also shows
     `__attribute__((noescape)) _Noescape` for one parameter.
   - `_Take(b->p)` on a `__counted_by_or_null(cap)` field does not count as assigning
     it, so `b->cap = 0` needs a redundant `b->p = NULL` (cJSON gap 10).
   - Passing `&u` to a `void *` parameter is reported as a write to `u` by the
     mutable-global check ("'u' is written here"), which is true only if the
     callee writes through it.
   - Under Apple clang, adding `_Nullable` to some pointers in ini.h turns on
     `-Wnullability-completeness` for the rest of the header (warnings only; lz4.h
     has the same).
