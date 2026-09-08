# Findings index

Everything found so far, what was checked and came back clean, and what has not
been looked at. **Read this before starting a new codebase** — the negatives are
here precisely so nobody spends a day re-deriving them.

Reproduce any of it with [`repro/`](repro/): `cd repro && ./run-all.sh`.
The *shapes* behind these findings, and the detectors built from them, are in
[`PATTERNS.md`](PATTERNS.md) — start there if you are opening a new codebase:
`./hunt.sh ~/your-tree`.

## Findings

| # | Where | What | Class | Repro |
|---|---|---|---|---|
| 1 | zstd `ZSTD_overlapCopy8` | forms a pointer up to 8 bytes **before** the output buffer | real, reachable UB | [finding](zstd/findings/FINDING-overlapcopy8-oob-pointer.md) |
| 2 | zstd `BIT_initDStream` | forms `start + 8` past the caller's object; reachable from public `ZSTD_decompressBlock` | real, reachable UB | [`01`](repro/01-zstd-initdstream.sh) |
| 3 | zlib `inflate_table` | doc says size the array `2^bits`; do that and the body **writes past it** | doc defect, proven | [`02`](repro/02-zlib-inflate-table.sh) |
| 4 | expat `storeRawNames` | compares and subtracts a pointer `realloc` already freed | real UB (indeterminate value) | [`03`](repro/03-realloc-aliasing-scan.sh) |
| 5 | sqlite `fts3_unicode.c` +1 more | same shape as 4 | real UB (indeterminate value) | [`03`](repro/03-realloc-aliasing-scan.sh) |
| 8 | CPython `PyImport_ExtendInittab` | compares a pointer `realloc` freed; the guarded branch **dereferences** the stale one | real UB, worst path of the family | [finding](generalize/cpython/FINDING-import-inittab-freed-compare.md) |
| 9 | CPython `Modules/expat/xmlparse.c` | vendored copy of finding 4: the expat defect ships in every CPython | real UB (indeterminate value) | [finding](generalize/expat/FINDING-storerawnames-freed-pointer.md) |
| 6 | zstd `BIT_lookBits` | documents a bound 26 wider than its callee accepts | doc defect, not reachable | [ledger](zstd/EXPERIMENT-annotation-yield.md) |
| 7 | zstd `ZSTD_execSequence` | preconditions live in asserts that `-DNDEBUG` removes | doc defect | [finding](zstd/findings/FINDING-execsequence-implicit-preconditions.md) |

**They are all one shape: a pointer that is formed, compared or subtracted, but
never dereferenced.** ASan instruments loads and stores and walks straight past
every one; no fuzzer input distinguishes the case that matters (whether
`realloc` moved the block, whether a stream sits at a buffer's end). That is why
they are still in shipped code, and it is the argument for a prover rather than
another sanitizer.

## Checked, nothing found — do not redo these

| Where | Why it looked promising | What is actually there |
|---|---|---|
| zlib `inflate_fast` | `from = out - dist`, `dist` straight from the stream — identical to finding 1 | **Correct, and it is the model.** zlib computes the distance as an *integer* (`op = (unsigned)(out - beg)`) and checks `dist > op` before forming any pointer; in the window branch `out` advances by exactly `dist - op` first, so `from` lands on `beg`. This is the fix finding 1 needs, written decades ago. |
| zstd `FSE_readNCount` | bounds-heavy header parsing of untrusted input | Clean. [audit](zstd/findings/AUDIT-fse-readncount.md) |
| zstd `ZSTD_wildcopy` | the unbounded proof target | Memory-safe for every length, `0 of 205`, no `--unwind`. [UNBOUNDED.md](zstd/UNBOUNDED.md) |
| zstd `ZSTD_safecopy` | second loop-bearing function in the decode path | Unproved, and the recorded reason (FORCE_INLINE) was **wrong** — see the note in UNBOUNDED.md. Its actual blocker is unestablished. |

## The detector's false positives, and why there are none left

The realloc detector reported 6 hits with 1 real when it was written. It now
reports **4 across six source trees, and all 4 are real.** Each false positive
was a distinct confusion, each is now ruled out mechanically, and the taxonomy
is in [PATTERNS.md](PATTERNS.md#shape-b--a-pointer-read-after-realloc-freed-it).

Previously-reported hits that are **not** defects, kept here so they are not
re-triaged if a rule ever regresses:

| Hit | Why it is fine |
|---|---|
| `zlib contrib/puff/pufftest.c:81` | `buf = NULL;` is a *write*, not a read of the freed value |
| `zlib examples/enough.c:335` | reads `.len`, a `size_t`. The freed pointer is `.vec` |
| `redis src/zmalloc.c:563` | reads `size`, an integer argument |
| `redis src/rdb.c:2572` | inside `if (nv == NULL)` — the failure path, where the old block is still live and must be freed. Correct code |
| `jq src/jv.c:455` | reads `values.values_num`, the integer count |
| `sqlite src/printf.c:1233` | `p->nAlloc` is the size argument, not the pointer |
| `jq vendor/oniguruma/src/regexec.c:1770` | inside `if (IS_NULL(new_alloc_base))` — the failure path. The guard is a *macro*, which the detector did not recognise until it was taught to; see the blind-spot table |
| `quickjs quickjs-libc.c:470` | `p = realloc(buf,...)` is the opposite arm of `if (ctx) p = js_realloc(ctx,buf,...)`. Two alternative allocations, only one runs; not a read of a freed value |
| `cpython Modules/_elementtree.c:515` | the `memcpy` is in the `else` branch, reached only when no `realloc` happened. The detector's window crosses the branch; it does not model control flow |

The lesson generalises past this detector: **a filter that silences a finding is
worse than the noise it removes.** Tightening these rules once eliminated every
false positive *and* the real expat defect, because expat's guard returns and
everything after it is the success path. Every rule here was re-checked against
the known-real sites before it was kept.

## The detector's *blind* spots, which are worse than its false positives

A false positive costs a few minutes of reading. A blind spot prints nothing and
is recorded as a clean tree. Two were found by auditing the detector rather than
its output, and both had already put wrong entries in this file:

| Blind spot | How it showed up | Fixed by |
|---|---|---|
| Trees with no C in them | `~/git/postgres` is a TypeScript client. It was swept and reported clean; there was nothing to sweep | the `[coverage]` line, which names files scanned and calls seen |
| Allocation-failure guards written as macros | oniguruma writes `if (IS_NULL(p))`, not `if (!p)`. The failure path — where the old block is still live and the read is *correct* — was being reported as a finding | matching any null-testing call `\w*NULL\w*(p)` as a guard |
| Reallocators reached through a struct field | cJSON calls `p->hooks.reallocate(...)`; expat's own `REALLOC` expands to `parser->m_mem.realloc_fcn(...)`. The regex required the name to start the callee, so it counted **zero calls in files that have them** | an optional member prefix in the call pattern |

Every run now ends with, on stderr:

```
[coverage] <tree>: N C files, M realloc-shaped calls, K repaired-pointer sites, H hits
```

and says so loudly when `N` or `M` is zero. **A zero-hit sweep means nothing
until that line says the detector examined something.** This is the gate-audit
rule applied to our own tooling: a check that silently examines nothing reports
success forever.

## The weak tail of the realloc family

Three sites read a freed pointer only to compare it against a null constant it
cannot equal: `sqlite src/util.c:2215` (`if( pIn==0 ) pOut[1] = 2;`),
`sqlite ext/fts5/fts5_expr.c:1780`, and their amalgamation copies. When the old
pointer was null the call was a malloc and nothing was freed; when it was not,
this reads an indeterminate value to answer a question whose answer is already
known. Same class as findings 4, 5 and 8, no way to get a wrong result out of
it, not worth a patch on its own. Recorded so they are not re-triaged as new.

## Not looked at yet

Ranked by the property that actually yields: **pointer arithmetic near a buffer
boundary, on sizes an attacker controls.**

- **redis `sds.c`** — cloned, scouted, no harness. `sdsHdrSize(s[-1])` recovers
  the allocation base from a type byte *stored in the buffer*, so every sds
  function carries an unwritten precondition: `s` points just past an intact
  header. Nothing in the source says it.
- **jq `jv.c`** — the realloc detector has been run over it (one hit, triaged
  false above). Not yet annotated: refcounted values with pointer tagging are
  the interesting part and are untouched.
- **zlib `inflate.c` `updatewindow`** — window wrapping, `put - state->wsize`.
- **libpng, brotli, lz4** — same family as zstd, not cloned.

## How to add to this

1. Pick a function that does pointer arithmetic near a boundary on
   attacker-controlled sizes.
2. Write its **documented** contract in this grammar — what the comment
   promises, not what the code does. That gap is the finding.
3. Prove with `--pointer-overflow-check`. Without it every finding above
   reports clean.
4. Run the control: allocate the buffer at the size the contract promises, and
   again at what the code needs. Two numbers, one variable.
5. Record it here **whichever way it falls.** A clean result saves the next
   agent a day, which is worth as much as a finding.

Bucket it honestly — 1 is reachable UB, 2 is a contract weaker than the code
needs, 3 is a limitation of the prover — in
[the ledger](zstd/EXPERIMENT-annotation-yield.md). Most findings are 2.
