# CPython `PyImport_ExtendInittab`: compares a pointer `realloc` already freed

**Where:** `Python/import.c`, around line 2641.

**Class:** use of an indeterminate pointer value (C17 6.2.4p2). Same shape as
[expat `storeRawNames`](../expat/FINDING-storerawnames-freed-pointer.md), found
by the same detector.

```c
p = NULL;
if (i + n <= SIZE_MAX / sizeof(struct _inittab) - 1) {
    size_t size = sizeof(struct _inittab) * (i + n + 1);
    p = _PyMem_DefaultRawRealloc(inittab_copy, size);
}
if (p == NULL) { res = -1; goto done; }

if (inittab_copy != PyImport_Inittab) {              /* <-- both are freed */
    memcpy(p, PyImport_Inittab, (i+1) * sizeof(struct _inittab));
}
memcpy(p + i, newtab, (n + 1) * sizeof(struct _inittab));
PyImport_Inittab = inittab_copy = p;
```

The `p == NULL` check returns early, so the comparison runs only when the
reallocation succeeded and the old block may already be gone.

**Both operands can be the stale pointer.** The last line sets
`PyImport_Inittab` and `inittab_copy` to the same value, so on the second and
later calls the comparison is between two copies of one freed pointer. On the
first call `inittab_copy` is NULL, the realloc is a malloc, nothing is freed,
and the comparison is the intended true.

## Why this one is worse than the others in this family

Elsewhere in these findings a compiler that mis-decides the comparison produces
a wrong offset that is still numerically right. Here the true branch is
`memcpy(p, PyImport_Inittab, ...)` — a **read through** the stale pointer. The
comparison is what keeps the program out of that branch on every call after the
first. An optimizer entitled to assume `realloc`'s result does not alias its
argument may conclude the two operands differ, take the branch, and dereference
freed memory.

So this is the one site found so far where the indeterminate-value UB has a
short path to an ordinary use-after-free rather than to a silently-correct
offset.

## Reachability

`PyImport_ExtendInittab` is public C API, called by embedders to register
built-in modules before `Py_Initialize`. The realloc branch needs a second or
later call, which is the documented way to extend the table incrementally.

## Fix

Compare before the reallocation, or compare the saved flag rather than the
pointers:

```c
const int first_call = (inittab_copy == NULL);
p = _PyMem_DefaultRawRealloc(inittab_copy, size);
if (p == NULL) { res = -1; goto done; }
if (first_call) {
    memcpy(p, PyImport_Inittab, (i+1) * sizeof(struct _inittab));
}
```

Not reported upstream.

## CPython also ships the expat defect

`Modules/expat/xmlparse.c:3076` is the vendored copy of
[the expat finding](../expat/FINDING-storerawnames-freed-pointer.md), so that
defect is present in every CPython build, not only in libexpat.
