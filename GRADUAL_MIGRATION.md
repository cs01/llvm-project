# Gradual Migration Guide

Nullsafe Clang works as a drop-in analysis tool. You don't need to change your build system or swap compilers — just point it at your existing `compile_commands.json`.

## Quick start: analyze without changing your build

If your project generates a compilation database (CMake, Bear, intercept-build, etc.), you can run Nullsafe Clang as a linter:

```bash
# Run against your existing compilation database
nullsafe-clang -fflow-sensitive-nullability -fnullability-default=nullable \
    -fsyntax-only -p compile_commands.json src/
```

This surfaces every potential null dereference in your codebase without touching your build flags, makefiles, or CI pipeline. Fix what you want, ignore the rest.

## Choosing a default nullability

There are three ways to adopt null-safety. Pick the one that fits your team:

### Option A: `nullable` default (maximum checking)

```bash
clang -fflow-sensitive-nullability -fnullability-default=nullable file.c
```

Every unannotated pointer is treated as nullable. You get warnings on every unchecked dereference. This is the strictest mode — good for finding all the bugs, but noisy on large unannotated codebases.

Mark pointers `_Nonnull` to suppress warnings where null is impossible:

```c
// With -fnullability-default=nullable
void process(int *p) {           // p is nullable (implicit)
    *p = 42;                      // warning: might be null
}

void process(int * _Nonnull p) { // p is nonnull (explicit)
    *p = 42;                      // OK
}
```

### Option B: `nonnull` default (ergonomic mode)

```bash
clang -fflow-sensitive-nullability -fnullability-default=nonnull file.c
```

Every unannotated pointer is treated as nonnull. No warnings unless you explicitly mark something `_Nullable`. Clean and quiet — good for new projects or codebases where most pointers shouldn't be null.

Mark pointers `_Nullable` where null is expected:

```c
// With -fnullability-default=nonnull
int * _Nullable find(int key);   // might return null

void caller() {
    int *result = find(42);
    *result = 0;                  // warning: result is nullable
    if (result) *result = 0;     // OK — checked
}
```

### Option C: Pragma-based (gradual, per-file or per-region)

```bash
clang -fflow-sensitive-nullability file.c
```

No default is set. Analysis only activates inside `#pragma clang assume_nonnull` regions or for functions with explicit nullability annotations. Everything else is untouched.

```c
// Unannotated — no warnings, no checking
void legacy(int *p) {
    *p = 42;  // no warning
}

// One annotation activates flow checking for this function
void checked(int * _Nullable p) {
    *p = 42;  // warning: p is nullable
}

// Pragma activates checking for an entire region
#pragma clang assume_nonnull begin

void also_checked(int *p) {
    *p = 42;  // OK — p is nonnull inside pragma
}

#pragma clang assume_nonnull end
```

This is the most gradual approach — one function or one file at a time. Add a single `_Nullable` annotation to a crash-prone function and it gets full flow checking. Everything else stays silent.

## Using nullsafe standard library headers

Nullsafe Clang includes nullability-annotated versions of `stdlib.h`, `stdio.h`, and `string.h` in `clang/nullsafe-headers/`. These annotate functions like `malloc` (returns `_Nullable`) and `free` (accepts `_Nullable`).

```bash
clang -fflow-sensitive-nullability -fnullability-default=nullable \
    -I/path/to/clang/nullsafe-headers/include file.c
```

This catches common mistakes:

```c
char *buf = malloc(100);
strcpy(buf, "hello");  // warning: buf might be null (malloc can fail)
```

## Flags reference

| Flag | Description |
|------|-------------|
| `-fflow-sensitive-nullability` | Enable the analysis (required) |
| `-fnullability-default=unspecified` | Default. No warnings on unannotated code |
| `-fnullability-default=nullable` | Unannotated pointers are nullable. Maximum checking |
| `-fnullability-default=nonnull` | Unannotated pointers are nonnull. Ergonomic mode |
| `-Werror=flow-nullable-dereference` | Treat null dereference warnings as errors |

## IDE integration

The fork includes `clangd`, so you get real-time warnings in your editor. See the main [README](README.md#ide-integration) for setup.
