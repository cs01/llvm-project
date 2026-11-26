# parson under Safe C

Source: kgabis/parson ec53fb6 (1.5.4): parson.c (2,494 lines), parson.h (274), tests.c
(945) and tests/. The pristine copy is the first commit of this directory; `git log -p
-- .` shows the port. Compiler: build.snap12 (52aaaf53, assertions on), macOS 26, arm64.

Result: finished. parson.c and tests.c compile with 0 errors under `-fsafe-c`, and all
362 upstream checks pass in all four configurations, in both of upstream's builds
(normal, and `PARSON_FORCE_HASH_COLLISIONS`). 490,000 fuzz iterations (mutated JSON
through both parsers, both serializers, deep copy, equals, validate and random edits)
gave the same output hash as pristine parson built with Apple clang; the first 40,000
also under ASan+UBSan. (The fuzzer was scratch code and is not kept here.)

It should not be read as "Safe C handles parson well". The annotations were the cheap
part. 21 `__unsafe`, a quarter of the file rewritten, a compiler crash worked around in
nine places, a correct program that trapped at run time, and two memory bugs that a
fully checked caller can still write through the ported API.

## Cost

Lines changed (a changed line counts once as added and once as removed):

| file | before | after | added | removed | still different with annotations and spacing ignored |
|---|---|---|---|---|---|
| parson.c | 2,494 | 2,562 | 874 | 806 | +654 -586 |
| parson.h | 274 | 276 | 107 | 105 | +4 -2 |
| tests.c | 945 | 966 | 114 | 93 | +42 -21 |

So a third of parson.c's lines changed, and about a quarter changed in their code, not
only in annotations. parson.h is almost pure annotation.

Annotations by kind (tokens):

| | parson.c | parson.h | tests.c |
|---|---|---|---|
| `_Nullable` | 319 | 169 | 71 |
| `_Owned` | 84 | 3 | 15 |
| `_Noescape` | 71 | 44 | 6 |
| `_Returns_owned` | 50 | 15 | 6 |
| `_Exclusive` | 49 | 29 | |
| `__null_terminated` | 38 | 3 | 12 |
| `_Take` | 30 | | |
| `__counted_by` / `_or_null` | 33 | 8 | 1 |
| `_Lifetimebound` | 11 | 8 | |
| `_Unchecked` | 7 | | |
| `_When` / `_Tagged_by` | 7 | | |
| `__bidi_indexable` | 5 | | |
| `_Unsynchronized` / `_Thread_local` | 5 | | 7 |
| `__null_terminated_to_indexable` | 5 | | |
| `_Takes` | 3 | 1 | |
| `_View`, `alloc_size`, `_Static` | 3 | | 1 |
| `__sized_by_or_null`, `__unsafe_indexable` | 2 | 2 | 8 |
| `__single` | | | 44 |

About 720 in a 2,500-line file, one per 3.5 lines. 319 + 169 `_Nullable` mostly record what
parson already handles.

### `__unsafe`: 21 in parson.c, 4 in tests.c

| count | where | why |
|---|---|---|
| 7 | read_file, parson_strndup, get_quoted_key, with_comments, to_string (2), get_string | "this buffer ends in 0" after code that just wrote the 0 |
| 4 | the json_set_* functions | writes to `_Unsynchronized` settings (and the compiler then warns that the `__unsafe` is unused) |
| 3 | json_object_store_name, _store_value, json_array_store | defect 3: `a->items[i] = owner` |
| 3 | get_wrapping_value (2), get_parent | back pointers |
| 1 | json_value_init_string_no_copy | defect 4: no way to fill a struct member of a tagged union that owns a counted buffer |
| 1 | json_serialize_number | user-supplied printf format |
| 1 | parson_malloc (function) | user-supplied malloc hook |
| 1 | adopt_value (function) | ownership taken only on success has no spelling |

Only 5 of the 21 (hook, format, back pointers) are for things that really cannot be
checked. In tests.c: argv into a `_Static`, the embedded-zero memcmp, and 2
terminators.

### Restructuring instead of annotating (parson.c)

Forced, and a maintainer would probably take it:

1. Serializer: `char *buf` with no size, NULL meaning "count only", became a
   `JSON_Writer {buf, cap, pos}`. Found two real bugs (below).
2. `parson_sprintf` (vsprintf) is gone: numbers go through a 64-byte buffer with snprintf.
3. `json_object_add` and `json_array_add` take their arguments on failure too, which
   removed cleanup code in four callers.
4. `dotset_value` does everything that can fail first, so the un-insert path
   (`dotremove_internal(..., free_value = 0)`) is gone.
5. `json_object_grow_and_rehash` moves entries in place of building a second object.
   `json_object_init` and `_deinit` (with their free_keys/free_values flags) are gone.
6. `process_string`, `parse_utf16`: output pointer became buffer plus index.

Forced, and a maintainer would ask why:

7. `string[1]`, `s[3]`, `*string += 4` on a terminated string are not allowed, so
   `parse_utf16_hex`, `is_decimal`, the BOM check, `hash_string` and the true/false/null tokens
   were rewritten to walk one character at a time (`skip_chars`).
8. `remove_comments` uses indexes and its own `token_at` in place of strncmp/strstr.
9. `JSON_String *` borrowed from the union became a `_View` struct (`JSON_Chars`) by value.
10. `memcpy`/`memmove` of owned pointer arrays became loops of `_Take`.
11. Nested calls split into named locals (defect 5).

Only to avoid compiler defects:

12. Nine functions split so that the part indexing an owned array takes a non-null
    parameter (the crash): name_at, value_at, array_value_at, findn_value, remove_at,
    json_array_take, both clear_items, json_object_put.
13. The first version returned strings through a `JSON_String *_Exclusive`; that compiled and
    trapped at run time (defect 2), so it returns a `__bidi_indexable` pointer.

### API and behaviour changes

- Custom allocators and number functions must repeat the typedef's annotations to compile
  under Safe C. Nothing changes for plain C callers. ABI unchanged.
- `name` is non-null in the dotget, dothas, remove and dotremove functions. Upstream crashes
  on NULL there.
- `json_serialize_to_buffer(value, NULL, n)` is not allowed. Upstream crashes on it if
  the value holds a number.
- `json_parse_string_with_comments(NULL)` returns NULL instead of crashing (as
  `json_parse_string(NULL)` does).
- A number that formats to more than 63 characters makes serializing fail; upstream
  overflows its stack buffer. Custom number functions always get a 64-byte buffer; upstream
  gives them a pointer into the caller's output buffer.
- Output over 2 GB works; upstream overflows an `int`.
- tests.c: `main` returns 1 on failure (upstream returns 0 always), STREQ became a
  function, the number test's 32-byte buffer is 64, example code uses snprintf and `%255s`,
  counters are `_Thread_local`. No check was removed or loosened.
- **Upstream's own Makefile no longer works.** It builds with `-std=c89
  -pedantic-errors`. safec.h uses variadic macros and `inline`, and `_Nullable`, `[[]]` and the
  statement expression in `_Take` are extensions. Pristine: 0 errors. Port: refused at the
  first 20 by clang, about 40 by gcc-16. gcc-16 `-std=gnu11` and clang++ build it.
  MSVC, which parson supports, cannot build `_Take` at all (not tried here;
  adoption.md says so).
- Users of parson.h on plain clang get `-Wnullability-completeness` warnings
  (10 from one build) because some pointers are `_Nullable` and the rest are not marked.

## Tests

```
cjson safec: 18/18
jsmn safec: 1/1
lz4 safec: 1/1
inih safec: 16/16
parson safec: 1/1
parson-collisions safec: 1/1
std safec: 2/2
cjson safec-own: 18/18
jsmn safec-own: 1/1
lz4 safec-own: 1/1
inih safec-own: 16/16
parson safec-own: 1/1
parson-collisions safec-own: 1/1
std safec-own: 2/2
cjson system: 18/18
jsmn system: 1/1
lz4 system: 1/1
inih system: 16/16
parson system: 1/1
parson-collisions system: 1/1
std system: 2/2
cjson asan: 18/18
jsmn asan: 1/1
lz4 asan: 1/1
inih asan: 16/16
parson asan: 1/1
parson-collisions asan: 1/1
std asan: 2/2
```

One "1/1" is one program with 362 checks. Planted bugs, each reverted:

| planted | safec, safec-own | system | asan |
|---|---|---|---|
| names array allocated one element short | fail (trap) | fail (heap corrupted) | fail |
| deep copy's string buffer one byte short | fail (trap) | **passes** | fail |
| key freed again after the object took it | fail, at compile time: "object allocated by 'get_quoted_key' does not live long enough" | fail | fail |

## Real bugs in parson

Each was run against the pristine source.

1. Heap overflow when output passes 4 GB. `written_total` is an `int`.
   A 715,827,883-byte string of `\x01` (6 output bytes each):
   `json_serialization_size` returns **5**, and `json_serialize_to_string` then writes 4 GB
   into 5 bytes (SIGBUS). The port returns 4294967301. What caught it: not a message about
   the `int`, but having to give `buf` a size.
2. Stack overflow from `json_set_float_serialization_format("%.100f")`: vsprintf into 64
   bytes (ASan: stack write past the buffer, from json_serialization_size). parson.h warns
   about it. Caught by the rule that vsprintf is not available.
3. `json_parse_string_with_comments(NULL)`, `json_object_remove(o, NULL)` and
   `json_object_dotget_value(o, NULL)` crash, while their siblings check for NULL. Caught by
   nullability, as a question about what each parameter should say.
4. `json_array_append_value(a, root)` with root being `a`'s own value is accepted, and
   `json_value_free` then recurses until the stack overflows. Not caught (it is behind
   `adopt_value`); the port traps at run time instead.
5. `json_parse_string("-")` returns the number 0. Found by reading, not by Safe C; not changed.
6. tests.c: unchecked results in `strlen(buf)`, `strlen(serialized)`,
   `puts(serialized_string)` and `scanf("%s")` into 256 bytes, all caught. `main` returning 0 on
   failure and two temporary files removed by the wrong name were found by reading.

Ownership (about 260 annotations, most of the restructuring and all the compiler trouble)
found **no bug** in parson: every error path already freed the right things.
Bounds and nullability found all of 1, 2, 3 and 6.

## What checked callers can still do wrong

(Written for an earlier version of the checker: `_Exclusive` and the rules these examples get around are gone. Kept as a record.)

Both compile with no error and no `__unsafe` in the caller; both confirmed
by building the same caller plain with ASan.

```c
/* use after free: const is laundered through the getter */
static void wipe(const JSON_Value *v) {
  JSON_Object *_Nullable __single o = json_value_get_object(v); /* takes const, returns mutable */
  if (o) json_object_clear(o);
}
...
const char *_Nullable __null_terminated s = json_object_get_string(json_value_get_object(v), "a");
wipe(v);                      /* a shared borrow, so s is thought to be alive */
if (s) printf("%c\n", *s);    /* heap-use-after-free */
```

The compiler has the rule for a direct field (`clearn(h->n)` with `const H *h` is rejected:
"'h' is a shared borrow, but this parameter is '_Exclusive'") but not through a function
that returns a field of its const parameter. This is a soundness hole in Safe C,
with nothing in the library unsafe about it:

```c
typedef struct H { struct N *_Owned _Nullable n; } H;
struct N *_Nullable getn(const H *_Nullable h) { return h ? h->n : NULL; }
void clearn(struct N *_Exclusive n) { free(_Take(n->p)); }
void use2(const H *h) { struct N *_Nullable o = getn(h); if (o) clearn(o); }   /* accepted */
int bug(const H *h) { struct N *_Nullable o = getn(h); if (!o) return 0;
  int *_Nullable q = o->p; use2(h); return q ? *q : 0; }                       /* accepted */
```

```c
/* double free: append_value takes ownership only if it succeeds, which cannot be said */
json_array_append_value(a, n);
json_value_free(n);
json_value_free(v);           /* frees n again */
```

That one is the mistake parson's header warns about most (five comments), and the
port does not catch it. Caught at compile time: freeing a child obtained from a getter, and
using a name or value after its root is freed.

## False errors and defects, by cost

1. **Crash: indexing an owned counted field through a `_Nullable` parameter.** Any use,
   with the null check present. Cost: nine split functions, one bisection
   by script (a crash names no function). Minimal:
   ```c
   struct Z { size_t *_Owned __counted_by(cap) a; size_t cap; };
   size_t f(const struct Z *_Nullable o) { if (!o) return 0; return o->a[0]; }
   // Assertion failed: (LHS.isValid() && RHS.isValid() && "Passed invalid source
   // location!"), function isBeforeInTranslationUnit, file SourceManager.cpp, line 2043.
   ```
   The stack is in `NullabilitySafetyReporter::finishFunction`, sorting diagnostics, so there
   is a false nullability diagnostic with no location behind it. Not checked
   without assertions. A local `const struct Z *p = o;` avoids it.

2. **Correct code traps at run time: `_Take` of a counted field gives a pointer with zero
   bounds.** It compiles silently. `_Take` zeroes the count before the result's bounds
   are taken, so the result can only be freed. Nothing safe moves a counted buffer from one
   owner to another.
   ```c
   typedef struct S { char *_Owned _Nullable __counted_by_or_null(size) chars; size_t size; } S;
   static void own(char *_Owned __counted_by(n) c, size_t n) { c[n-1] = 0; free(c); }
   ...
   size_t n = s.size;
   char *_Nullable _Owned c = _Take(s.chars);
   if (c == NULL) return 1;
   own(c, n);              /* Bounds check failed, n is 8 and c came from malloc(8) */
   ```

3. **Storing an owner in an element of an owned array is rejected.** This is the body of
   every vector of owned pointers.
   ```c
   typedef struct A { struct N *_Owned _Nullable *_Owned _Nullable __counted_by_or_null(capacity) items;
                      size_t count, capacity; } A;
   void add(A *_Exclusive a, struct N *_Owned v) { if (a->items) { a->items[0] = v; } else nfree(v); }
   // error: this pointer is borrowed from 'a->items', and the borrow ends (the field is taken,
   //   replaced, or lent to an exclusive parameter) before its last use; read 'a->items' again
   //   after that point, or finish using the pointer first
   ```
   "The borrow ends here" and "later used here" point into the same assignment. Nothing can
   be read again. A helper `*slot = v` with `T *_Owned _Nullable *_Exclusive slot`
   is rejected too, with advice to do what it already does ("store it as an owner
   ('_Owned *' parameter)"). 8 stores, now 3 `__unsafe`.

4. **A struct member that owns a counted buffer cannot be put in a tagged union.** Setting
   the tag and then `.chars`, `.size`: "the tag 'type' now names 'string', but 'string'
   has not been written since the tag changed". Assigning a struct: "copying 'JSON_String'
   duplicates its owned field". A literal by name: "write '_Take(c)'". A literal with
   `_Take`: "initalizer for '__counted_by_or_null' pointer with side effects is not yet
   supported" (sic). Nine forms tried, all rejected. 1 `__unsafe`.

5. **Callers of any handle library pay the most.** tests.c needed, in code that is
   ordinary C:
   - `__single` on every local of an opaque type (44). `V *v = get();` with `typedef
     struct V V;` is "cannot initialize indexable pointer with type 'V *__bidi_indexable'
     from __single pointer to incomplete type; consider declaring pointer 'v' as
     '__single'". An incomplete type cannot be indexed, so `__single` is the only
     thing that can be meant.
   - A nested exclusive argument is rejected: `json_object_set_string(json_object(v),
     "name", s)`, the first example in parson's README, gives "which object this call's
     result reaches cannot be told, so what borrows it would not end; name the object directly".
     Every argument here is a named local and one getter.
   - A parent cannot be used after a child was edited: `arr = get_array(obj, "x");
     append(arr, 1); set(obj, "y", 2);` ends the borrow of `obj`, because editing `arr` ends
     everything borrowed from the root. Rust accepts this (arr is a reborrow of obj).
     6 re-fetches added.

6. **`const char *` means terminated in a parameter and not in a local.**
   `const char *d = strchr(s, '.');` is rejected: "requires a linear search for the
   terminator; use '__null_terminated_to_indexable()' to perform this conversion
   explicitly". 10 locals needed `__null_terminated`.

7. **Nullability is not narrowed by assigning a non-null result** to a `_Nullable` local:
   ```c
   struct N *bor(void);
   void f(void) { struct N *_Nullable n = NULL; n = bor(); n->v = 1; }
   // error: dereference of nullable pointer
   ```
   C89 code declares first and assigns later, so this would be common there.

8. **`_Owned` on a parameter in a function pointer typedef is ignored**, and so is
   `_Takes(1)` there. Only `_Takes(1)` on each variable or parameter of that type works.

9. A terminated-string pointer that steps one past the terminator and is never read traps
   (`*p++ == 'x'` at the end). Legal C, and an idiom parson uses. My own BOM check hit it on
   the empty string; it would be a denial of service found only by running.

10. A pointer parameter that a callee only fills (`size_t *length`, beside `const char
    **string`) needs `_Noescape`, or "a borrow of local variable 'input_string_len' is stored
    where 'string' points". A `size_t *` cannot be stored in a `const char *`.

## Messages that sent me the wrong way

- `'output' is '_Owned', so it must be given a pointer it may free ...; this value is
  borrowed` at 15 call sites. The one fix was `_Returns_owned` on my `alloc_size`
  wrapper, which the message never names. Worst ratio of errors to cause.
- `'struct N' holds pointers, and as a 'void *' kept here its bytes could be overwritten
  (memcpy, memset, read), forging them` at 11 calls of the free hook. The cause was item 8
  above: the hook was not known to take ownership. Nothing is kept or overwritten.
- `local variable 'obj' does not live long enough ... destroyed here`, pointing at a call in
  the middle of the function where `obj` is in scope and only read. The real
  rule is item 5's third point. I tried annotating the other parameter (`_Unchecked`:
  11 new errors, no help) before finding it with ten probes.
- `unsafe block contains nothing Safe C would reject; remove '__unsafe'` on writes to
  `_Unsynchronized` globals. Removing it: `put this write inside '__unsafe { }'`. Both
  from the same compiler on the same line:
  ```c
  static int lvl _Unsynchronized = 1;
  void set(int x) { __unsafe { lvl = x; } }   /* warning: remove __unsafe */
  void set(int x) { lvl = x; }                /* error: put it in __unsafe */
  ```
- `nothing says how much memory 'strncmp' accesses` and `drops its bounds` for a local
  that only lacked `__null_terminated`; fixing the declaration removed both. The same
  happens with `'memcpy' is checked only when called by name`, reported on a plain call to
  memcpy whose source was a terminated string.

Good messages: the `_Unchecked` ones, "Use vsnprintf instead", `_Takes(1)` on the hook variable,
and all the double free and use-after-free ones.

Could not tell what to write: how to return an owned buffer with its length (tried an
`_Exclusive` out-struct, which trapped; `__bidi_indexable` is not in annotations.md at all);
how to fill the tagged union; what to put on a parameter that is taken only on success.

## Log

Agent time, not human time: 16:27 clone and reading, 16:35 first full rewrite compiled, 16:38
parson.c at 0 errors, 16:40 tests pass, then gates, fuzzing and measurements until about 16:52.
The port was typed in as whole functions after reading the cJSON and inih notes. My guess (a
guess, nothing measured) for a C programmer new to Safe C is several days.
19 logged compiles of parson.c and tests.c (0.05 to 0.2 s each), 8 batches of small probes
(about 75 files), one 164-compile bisection for the crash.

| round | parson.c errors left | main kinds |
|---|---|---|
| 0, pristine | 332 | 191 index or arithmetic on a one-element pointer; 13 index on a terminated string; 21 casts of `void *`; 19 "buffer passed as a terminated string"; 12 untracked fields; 12 union members; 5 globals |
| 1, all signatures, types and bodies rewritten (a script bug in my patch tool cost one extra cycle at 122) | 59 | 20 "'x' is _Owned ..." (one missing annotation); 21 terminated-string locals; 5 tagged union; 3 `_Unchecked` |
| 2, hook typedefs | 60 | plus 12 "holds pointers ... void *" |
| 3, `_Returns_owned`, `_Takes`, `__null_terminated` locals | 30 and a crash | 10 free hook on a terminated string; 8 element stores; 9 "borrow stored where 'string' points"; 1 struct copy |
| 4, store helpers, `_Noescape`, split functions | crash, no errors shown | bisected to json_object_getn_value |
| 5 | 3 | 2 narrowing (7 above), 1 of mine (a null check I dropped) |
| 6 | 0 | 4 contradictory `-Wunused-unsafe` warnings |
| 7, run | trap | `_Take` zero bounds (defect 2) |
| 8, run | trap | one past the terminator (defect 9) |
| 9, run | 0, 362 of 362 | |

tests.c: 131, then 66 after a regex added `__single` and `_Nullable`, then 23 (17 of them
the STREQ macro evaluating a call twice), then 0.

Which rules cost the most: ownership first (restructurings 3, 4, 5, 10, 12, 13, defects 1 to 5,
9 of the 21 `__unsafe`), terminated strings second (7, 8, 11 of the errors in every round, 7
`__unsafe`), nullability third in volume (559 tokens) but cheap each, plain bounds least.
Ownership found nothing in this library. Bounds and nullability found everything.

## Speed

6.3 MB of JSON, best of 12 parse and serialize passes per run, 7 runs of each build
interleaved, medians in ms. Load average 1.9 to 3.3 on the machine, so slightly busy; runs
agreed to about 3 percent. All at -O2; the caller is the same Apple clang object file.

| | pristine, Apple clang | port, Apple clang | port, fork without -fsafe-c | port, -fsafe-c | Safe C vs pristine |
|---|---|---|---|---|---|
| mixed, parse | 29.2 | 32.0 | 32.3 | 38.7 | +33% |
| mixed, serialize | 119.4 | 121.2 | 120.8 | 123.6 | +3.5% |
| strings, parse | 17.4 | 18.9 | 19.0 | 23.0 | +33% |
| strings, serialize | 16.7 | 17.0 | 16.6 | 21.0 | +26% |

About 9% of the parse cost is the rewrite itself (walking strings one character at a time,
clearing new arrays), and 21% is the checks. Mixed serialize is mostly snprintf of doubles. Text
size of parson.o: 24.9 KB pristine, 23.9 KB port, 30.1 KB with checks.

## Would a maintainer accept this?

No. Not this diff.

- It breaks his own build (`-std=c89 -pedantic-errors`) and MSVC. That alone ends it.
- One annotation per 3.5 lines, in a style he has to learn (four kinds of "pointer to
  characters"), for a compiler that has no release.
- It is slower by a third on parsing.
- It asks every user who turns Safe C on to write `__single` on every handle and to
  un-nest the calls in his README.
- And it does not check the thing he warns users about most, who owns a value after
  `set_value`/`append_value`.

He would accept three pieces without any Safe C: the writer struct (fixes the 4 GB
overflow), snprintf for numbers, and `json_object_add` consuming its arguments. He
would probably accept parson.h's `_Nullable` alone if the macros worked in C89.

Could a working C programmer live with it? With bounds and nullability, yes: those errors
were right, the fixes were local, and they found real bugs. With ownership as it is in
this compiler, no. I hit a crash, a run-time trap on correct code, and
three patterns with no accepted spelling (store an owner in an array, move a counted
buffer, put an owner in a union), all of them in the first data structure this port reached. I got
through because I could write probes and knew which errors to disbelieve. Someone
who trusts the messages would not finish.

The three changes that would help most:

1. **Make owned containers work without `__unsafe`**: `a->items[i] = owner`, `_Take` keeping
   the bounds of a counted field, a struct with an owned counted field as a tagged union
   member, and the `_Nullable` crash. One JSON value and one growable array of them
   should be a regression test; it would have caught all four.
2. **Infer more at callers and in locals**: opaque-type pointers are `__single`; `const char *`
   locals are terminated when initialized or assigned from a terminated string;
   assignment narrows nullability; nested exclusive arguments and child reborrows are accepted.
   This removes 60 or more of the marks in tests.c and is what users of a library see.
3. **Say "takes ownership only on success"** (for example `_Owned_if(result == 0)`), and close the
   const getter hole. Without these the ownership annotations cost the most and do
   not protect the API's main hazard. Also needed, though not a checker change: a safec.h
   that a C89 pedantic build and MSVC accept.

## Ownership on (2026-11-25)

parson.c compiles with the ownership checker on; its tests run with it off. `__unsafe` 21 to 7. 19 `_Trusted` functions
(13 are two macros' instances). None knowingly breaks its signature. Two rest on something only parson.c's own code keeps true: `replace_float_format`
(nothing else stores into `settings.float_format`) and `string_from_chars` (the byte after the counted characters is the terminator).

Behavior changes: `json_object_set_value`, `json_object_dotset_value`, `json_array_append_value` and `json_array_replace_value` take the
value always and free it on failure (**callers that free on failure now free twice**), except a value that already has a parent, which is
left where it is. `json_value_get_parent` and the two `get_wrapping_value` return an `_Unchecked` pointer: to compare, not to follow,
because the result is not inside the argument. New arrays are zeroed. Nothing else: a randomized trace of 600,000
calls is identical to the version before (checks/fuzz.sh).
