# sqlite: two reads of a pointer `realloc` has already freed

**Where:** sqlite at `~/git/sqlite`, both present in the amalgamation and in the
`src/`, `ext/` and `tsrc/` copies.

**Class:** use of an indeterminate pointer value (C17 6.2.4p2). The same defect
as [expat `storeRawNames`](../expat/FINDING-storerawnames-freed-pointer.md),
found by turning that finding into a detector and pointing it at other C.

## 1. `fts3_unicode.c:350` -- both operands of a subtraction are freed

```c
if( (zOut-pCsr->zToken)>=(pCsr->nAlloc-4) ){
  char *zNew = sqlite3_realloc64(pCsr->zToken, pCsr->nAlloc+64);
  if( !zNew ) return SQLITE_NOMEM;
  zOut = &zNew[zOut - pCsr->zToken];   /* <-- both point into the old block */
  pCsr->zToken = zNew;
  pCsr->nAlloc += 64;
}
```

The NULL check returns early, so the subtraction runs only when the allocation
succeeded and the old block may already be gone. `zOut` and `pCsr->zToken` both
point into it. The difference is the right number; the code is not allowed to
compute it from those two values.

Fix: take the offset before the call.

```c
const sqlite3_int64 iOff = zOut - pCsr->zToken;
char *zNew = sqlite3_realloc64(pCsr->zToken, pCsr->nAlloc+64);
if( !zNew ) return SQLITE_NOMEM;
pCsr->zToken = zNew;
zOut = &zNew[iOff];
```

## 2. `fts5_hash.c:332` -- a list walk compares against the freed pointer

```c
pNew = (Fts5HashEntry*)sqlite3_realloc64(p, nNew);
if( pNew==0 ) return SQLITE_NOMEM;
pNew->nAlloc = (int)nNew;
for(pp=&pHash->aSlot[iHash]; *pp!=p; pp=&(*pp)->pHashNext);   /* <-- */
*pp = pNew;
p = pNew;
```

The loop scans the hash chain for the slot still holding the old pointer, so it
compares every stored pointer against `p` -- whose value became indeterminate
the moment `sqlite3_realloc64` succeeded and freed the block. The stored
pointers it compares against are fine; the operand `p` is not.

Fix: find the slot before the reallocation, or compare against a
`uintptr_t` captured before the call.

## Why nothing caught these

sqlite is plausibly the most-tested C in existence, and that is the point.
Neither site dereferences the freed pointer -- one subtracts it, one compares
it -- so ASan, which instruments loads and stores, sees nothing. Whether
`realloc` moves the block is the allocator's choice, not an input, so no fuzzer
corpus distinguishes the two cases. The test suite cannot fail on this, however
thorough, because there is no observable behaviour to fail on until a compiler
decides to exploit the indeterminacy.

That CBMC flags this class is shown in
[`probe-freed-pointer-subtract.c`](probe-freed-pointer-subtract.c):

```
[harness.pointer_arithmetic.9] line 15 pointer relation:
    deallocated dynamic object in buf: FAILURE
VERIFICATION FAILED
```

**Not yet proved by CBMC against sqlite itself.** Both sites above were found by
a retired standalone Python detector, not by the contracts feature, and
confirmed by reading the guards by hand. A harness over the real functions is
the honest next step and is not written; until it is, these are hand-verified
reports, not proofs, and they are recorded here at that strength.

Not reported upstream.

## A third, weaker one

`fts5_expr.c:1780` (and its copies) does `if( pPhrase==0 ) memset(pNew, ...)`
after `sqlite3_realloc64(pPhrase, ...)`. When `pPhrase` was null the call was a
malloc and nothing was freed; when it was not null, this reads a freed value to
compare it against a constant it cannot equal. Same class, no way to get a wrong
answer out of it. Recorded for completeness, not worth a patch on its own.

## What the retired detector found and did not find

The retired detector reported the argument a `realloc`-shaped call later
repairs from its own result, then flagged reads of that argument in between.
Using the repair line to identify which argument was the pointer kept the
allocator handle and the size expression out of the results.

Its dominant false positive is the allocation-failure branch:

```c
pNew = sqlite3DbRealloc(db, pList, ...);
if( pNew==0 ){ sqlite3ExprListDelete(db, pList); return 0; }   /* legal */
pList = pNew;
```

On failure `realloc` leaves the old block alone, so reading `pList` there is
correct, and sqlite does this deliberately and everywhere. The detector does not
model the guard; every hit needs the guard read by hand. On the four codebases
swept -- redis, jq, sqlite, quickjs -- everything except the three sites above
was that pattern or a stem-matching artifact.

Validation: run it on libexpat and it reports `storeRawNames` and nothing else
in 9436 lines. A detector that cannot find the bug it was derived from is worth
nothing, so that check comes first.
