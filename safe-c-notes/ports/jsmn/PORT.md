# jsmn under Safe C

Source: zserge/jsmn 25647e69 (471-line header-only JSON tokenizer). The
pristine copy is the first commit of this directory; `git log -p -- .` shows
the whole port.

Result: library and its 16 tests compile under `-fsafe-c` with no errors and
pass under Safe C, Apple clang and gcc. Same source, no `#ifdef SAFE_C`.

## Library (jsmn.h): 4 signatures, 0 `__unsafe`

| What | Count |
|---|---|
| `const char *__counted_by(len) js` (the input text) | 4 |
| `jsmntok_t *_Nullable __counted_by_or_null(num_tokens) tokens` | 3 |
| `jsmntok_t *__counted_by(num_tokens)` + `_Nullable` return on `jsmn_alloc_token` | 1 |
| `_Lifetimebound` on `jsmn_alloc_token`'s `tokens` (it returns `&tokens[i]`) | 1 |
| macro fallbacks so the header builds without Safe C | 11 lines |

No code changes. Every index into `js` and `tokens` is now bounds-checked.

## Tests (testutil.h): 14 changed lines, 7 `__unsafe`

- `__counted_by(numtok)` on the token parameters of `vtokeq`/`tokeq`.
- `_Nullable` on `s`, which the tests pass as NULL.
- `__null_terminated_to_indexable(s)` before slicing the input (checked scan
  for the terminator; string literals only allow `p++` otherwise).
- 7 `__unsafe` lines, all `va_arg`: nothing can check the count or types a
  caller passed through `...` (Safe C requires `__unsafe` for every `va_arg`
  since the 2026-11-23 audit); this is the honest unsafe boundary.
- `strncmp(slice, value, n)` became `memcmp`: the slice has no terminator
  inside its bounds, and the length is already known.
- **Real bug found:** `parse()` used `malloc` without a null check.

## Compiler fixes it drove

- Null checks now carry through `__null_terminated_to_indexable()`
  (`clang/test/Sema/safe-c-nullability-terminated-conversion.c`).
- `safec.h` gained no-op fallbacks for the forge and conversion macros, so a
  vendored copy builds on compilers without `ptrcheck.h`.

## Open usability gap

`strncmp` on a bounded slice: its contract (terminator or `n` bytes, whichever
comes first) has no bounds annotation. Apple's SDK marks it unsafe-indexable;
`safec.h` marks it `__null_terminated`, which traps on a slice. A `safec.h`
wrapper written in Safe C (bidi-indexable params, stop at NUL or `n`) would
check exactly what it reads.
