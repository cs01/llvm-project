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
- 9 `__unsafe` sites (after the stricter libc, builtin and const rules landed; 2 are the const-cast helpers cJSON's API needs):
  - `hook_allocate`, an `__unsafe` function marked `alloc_size`: it vouches that
    the user-supplied allocation hook returns fresh memory. Everything else
    allocates through it with no `__unsafe`, because Safe C checks that an
    `alloc_size` function returns only fresh memory.
  - `reallocate_bytes`: the same trust for the realloc hook.
  - 5 terminator conversions (`__unsafe_null_terminated_from_indexable` after
    writing the NUL, and `cJSON_Minify`, which rewrites the terminator through a
    view). Making these checkable is the open "keep the scan bound" item.
- Ownership: 7 annotated lines (`_Takes`): the `free_fn`, `deallocate` and
  `reallocate` hook fields, and `cJSON_Delete`, `cJSON_free`,
  `cJSON_ReplaceItemViaPointer` and `reallocate_bytes`, each found by the compiler
  ("frees its parameter, but callers are not told"). Using a tree after
  `cJSON_Delete` is now a compile error, including frees through the user's hooks.
- `lifetimebound`: 5 parameters (functions that return their argument or a
  pointer into it: the whitespace and BOM skippers, two const-cast helpers,
  `cJSON_DetachItemViaPointer`). Getters that return a child loaded from a
  field (`cJSON_GetObjectItem`) are not flagged yet: fields are the open gap.
- `sprintf`/`strcpy` into counted buffers became `snprintf`/`memcpy`.
- `strncmp` on input slices became `memcmp` (same gap as jsmn).

Compared with jsmn (4 annotated signatures), cJSON costs more because it is
a tree of nullable pointers, owns strings, and allocates through hooks. About
60% of the annotations are `_Nullable`, which mostly records what the code
already handled.

## M2 ownership (2026-11-23)

cJSON.c compiles with 0 errors under `-mllvm -safe-c-ownership` (26 before
annotating, 112 once the fields were). 18/18 tests pass under Safe C with and
without the flag, Apple clang, `gcc` (Apple clang on this Mac, no real gcc
installed) and Apple clang with ASan and UBSan.

Cost: cJSON.c +502/-509 lines, cJSON.h +51/-47, tests +25/-21. Kinds used in
cJSON.c and cJSON.h: 51 `_Owned`, 85 `_Exclusive`, 7
`_Unchecked`, 1 `_View` (the parse buffer), 22 `_Take`,
38 `_Lifetimebound`, 38 `_Returns_owned`. Tests: 2
`_Static`, 2 `_Exclusive`, 6 `_Take`.

Design:
- `next` and `child` own; `prev` is `_Unchecked` (the first child's prev
  is the tail). All prev surgery is in `append_child` (O(1) append, used by the
  parser, the array builders, Duplicate and AddItem*) and in the detach, insert
  and replace functions, each one `__unsafe` block, as Rust's LinkedList does.
- `valuestring` and `string` own. Reference items and constant keys store
  borrowed pointers into them through `adopt_item`/`adopt_string`; cJSON_Delete
  forgets them (`_Take` without free) when the flag says so.
- The public AddItem*/InsertItem/ReplaceItem functions keep a plain `item`
  parameter: they take the item only when they succeed, and the tests rely on
  that. `adopt_item` makes it owned after the last failure point. The
  `cJSON_Add*ToObject` helpers return a borrow of `object` (lifetimebound),
  not owned memory as the old annotation claimed.
- `item` in DetachItemViaPointer/ReplaceItemViaPointer is `_Unchecked`:
  it names a child by identity, so callers may pass a borrow of `parent`.
- `reallocate_buffer` works like reallocf (frees on failure) and takes the
  printbuffer, not the pointer. PrintPreallocated puts the caller's buffer in
  the owned field and takes it back unfreed.
- The global error pointer is unchecked: it points into the caller's input
  after the parse returns.
- Tests: 7 white-box test functions (lists built in stack arrays, deliberate
  misuse such as adding an item to itself) became `__unsafe` functions. M2
  rejects that misuse at compile time.

`__unsafe` sites in cJSON.c: 9 before, 19 after the port, 12 after the compiler fixes
below (the 3 field getters and `internal_realloc` need none now). The rest: `append_child`, detach, insert and replace (prev
surgery, gap 6), PrintPreallocated (gap 4), GetErrorPtr (unchecked global). The
2 const-cast helpers became `adopt_item`/`adopt_string` (gaps 4, 8), and
`reallocate_bytes` became `reallocate_buffer`.

### M2 compiler gaps, by how much fixing them would help

Status (2026-11-23): fixed in the compiler: 1 (soundness), 2 (crash), 3, 5, 6 (cursor
roots), 7, 8, 9 (`ensure` now needs and has `_Lifetimebound`). Open: 4 (ownership that
depends on a flag or on success) and the small ones in 10.

1. Soundness: an `_Exclusive` parameter accepts a shared borrow, so a callee
   can free fields under a `const` pointer whose caller still holds borrows.
   `void clear(N *_Exclusive n) { free(_Take(n->child)); }`
   `void g(const N *a) { N *c = a->child; if (c) clear(c); }  /* accepted */`
2. Crash: `_Take(p->f)` where `p` is a local pointer or array (not a
   parameter) asserts in CodeGen (EmitScalarExpr) when passed to a hook.
   `struct B b = {0}; struct B *p = &b;`
   `h->dealloc(_Take(p->buf));  /* buf owned, counted_by_or_null */`
   Workaround: `b[0].buf`.
3. Control mode: without `-safe-c-ownership`, `lifetimebound` on a function
   returning a field of its parameter is "could not verify", and an `__unsafe`
   block does not lift it. With the flag it is accepted. 3 `__unsafe` getters.
   `N *_Nullable next_of(const N *n _Lifetimebound) { return n->next; }`
4. Conditional ownership has no spelling: fields owned unless a flag says
   otherwise (cJSON_IsReference, cJSON_StringIsConst, printbuffer.noalloc),
   and functions that take an item only on success (AddItemToArray, realloc on
   failure). Rust would use Cow/enum and `Result<(), Box<T>>`. 5 sites.
   `int add(L *_Exclusive l, N *item);  /* moves item only if it returns 1 */`
5. `realloc`, or any function that both takes and returns ownership, rejects
   an owned local or parameter as "freeing a pointer that is still stored in
   memory reached through a pointer".
   `_Returns_owned void *r(void *_Nullable _Owned p, size_t n)`
   `{ return realloc(p, n); }  /* error */`
6. No exclusive cursor: `slot = &(*slot)->next` on an `_Exclusive` local is
   rejected (treated like reassigning a parameter), so a singly linked list
   cannot be walked and edited safely; detach/insert/replace fall back to
   prev in `__unsafe`. Comparing an unchecked pointer (`item == p`) also needs
   `__unsafe`, though it reads no memory.
   `N *_Owned _Nullable *_Exclusive slot = &a->child;`
   `while (*slot) slot = &(*slot)->next;  /* 2 errors */`
7. A walk that returns a borrow, `c = c->next; return c;`, is reported as
   returning stack memory; the same walk through a lifetimebound helper is
   accepted (`next_sibling`).
   `N *_Nullable c = a->child; while (c && i-- > 0) c = c->next; return c;`
8. The borrow-return rule ("returns a pointer borrowed from 'item'") is not
   lifted by an `__unsafe` block, only by an `__unsafe` function.
   `_Returns_owned N *own(const N *p) { __unsafe { return (N *)p; } }`
9. Possible hole: `ensure()` returns `p->buffer + p->offset`, a borrow of an
   owned field, with no lifetimebound and no error; adding lifetimebound gives
   "could not verify".
10. Small: `c ? owned : owned` is not owned; `_Take` of a counted
    field does not count as assigning it, so the count update needs
    `p->buffer = NULL` too; returning an owned local needs an explicit
    `_Returns_owned`.

## Bugs found (tests, not the library)

- `tests/print_number.c:39`: the buffer points at a 26-byte array while its
  length says 1024.
- `tests/minify_tests.c:146`: `malloc` result is not checked.
- `tests/misc_tests.c`: about 20 unchecked `cJSON_Create*` results.

## Compiler gaps it found, by priority

Soundness:
1. Fixed: `strcpy`/`sprintf` into a `__counted_by(n)` buffer compiled and
   overflowed (Apple's libc annotations were off under `-fsafe-c`).
2. Fixed: `__unsafe_null_terminated_from_indexable`/`_to_indexable` were
   accepted outside `__unsafe`.

Usability (Safe C rejects or traps on safe code):
3. Fixed: a cast of a `__null_terminated` pointer to another character type gave
   `__single`, and then `u[1]` traps.
4. Allocator hooks: fixed. An `alloc_size` function counts as a fresh
   allocation and its body is checked; the hook pointer itself stays trusted in
   one `__unsafe` function.
5. Fixed: null checks on a `__counted_by_or_null` field did not narrow it.
6. Predicates (`cJSON_IsString(n) && n->valuestring`) need a way to say "true
   means the argument is nonnull".
7. Fixed (libc annotations): passing a `__null_terminated` pointer to `free(void *)` asked for a
   terminator scan.
8. Fixed: locals did not take `__null_terminated` from their initializer.
9. A view that includes the terminator needs an `__unsafe_` spelling.
10. endptr idioms (`strtod`, `return_parse_end`) have no thin type for "a
    pointer into the caller's buffer".
11. The definition must repeat every bounds annotation from the header.
12. Stack frames are about 16 times larger at -O0 (1,840 vs 112 bytes in
    `cJSON_Duplicate_rec`), so deep recursion overflows. At -O2 the frame is
    64 bytes.
13. `strncmp` on a slice (also found in jsmn).

## Ownership on (2026-11-25)

cJSON.c compiles with ownership on (test-ports.sh checks it); its tests run with it off. Written by one agent, reviewed here.
cJSON.c about 1,080 changed lines; `__unsafe` 20 to 8; 13 `_Trusted` functions, 85 lines.

**Not sound: references.** `adopt_item`, `adopt_string` and `create_reference` put a pointer cJSON does not own in an owner field, and only
`type` flags keep it from being freed. No mark says that. known-gaps/references.c has programs that compile and are use after frees.
Dropping the `cJSON_IsReference` test in `cJSON_Delete` is accepted too.

Behavior changes: AddItemTo*, InsertItemInArray and ReplaceItem* always take the item and free it on failure (**callers that free on failure
now free twice**); AddItemToArray and AddItemToObject walk to the tail (40,000 items: 0.001 s to 0.78 s); Detach and Replace via pointer reject
an item that is not a child; Duplicate copies a constant key.
