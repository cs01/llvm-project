# Gradual Migration Guide

You can run Nullability Safety over an existing codebase without changing your build system or switching compilers. Point the fork's `clang` at your `compile_commands.json` and it reports findings like a linter.

In this guide, `clang` means the fork's `clang` (see [Installation](README.md#installation)), not your system compiler.

## Quick start: analyze without changing your build

`-fsyntax-only` makes Clang parse and type-check your code, run the analysis, and report warnings **without producing object files**. That is what lets the fork act as an analysis tool next to your real compiler.

### One file

```bash
clang -fnullability-safety -fnullability-default=nullable \
    -fsyntax-only -I/path/to/includes file.c
```

### A whole compilation database

A `compile_commands.json` (from CMake with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, Bear, intercept-build, and others) records the exact compiler command for each file: include paths, defines, language standard, and so on. To analyze every file, rewrite each command:

1. **Replace** the compiler with the fork's `clang`
2. **Add** `-fnullability-safety -fnullability-default=nullable -fsyntax-only`
3. **Remove** `-c` and `-o <file>`, which don't apply with `-fsyntax-only`

> **Note:** `clang` has no `-p compile_commands.json` option; that belongs to LibTooling tools like `clang-tidy` and `clang-check`. You have to read the database and run `clang` once per entry.

A minimal script:

```python
#!/usr/bin/env python3
# analyze-compdb.py: run Nullability Safety on every entry in compile_commands.json
import json, shlex, subprocess, sys

CLANG = sys.argv[1] if len(sys.argv) > 1 else "clang"
FLAGS = ["-fnullability-safety", "-fnullability-default=nullable", "-fsyntax-only"]

for entry in json.load(open("compile_commands.json")):
    argv = entry.get("arguments") or shlex.split(entry["command"])
    args, skip = [], False
    for a in argv[1:]:
        if skip:
            skip = False
        elif a == "-o":
            skip = True
        elif a != "-c" and not a.startswith("-o"):
            args.append(a)
    subprocess.run([CLANG, *FLAGS, *args], cwd=entry.get("directory", "."))
```

Run it from the directory that contains `compile_commands.json`, passing the path to the fork's `clang` if it isn't first on your `PATH`:

```bash
python3 analyze-compdb.py ~/.local/null-safe-clang/bin/clang
```

This reports every potential null dereference in the codebase without touching your build flags, makefiles, or CI. Fix what matters and ignore the rest.

## Choosing a default nullability

There are three ways to adopt the analysis. Pick the one that fits your codebase.

### Option A: `nullable` default (most checking)

```bash
clang -fnullability-safety -fnullability-default=nullable -fsyntax-only file.c
```

Every unannotated pointer may be null, so every unchecked dereference warns. This finds the most bugs, and on a large unannotated codebase it produces the most warnings.

Mark pointers `_Nonnull` where null is impossible:

```c
// With -fnullability-default=nullable
void set(int *p) {              // p may be null (no annotation)
    *p = 42;                    // warning
}

void set_nonnull(int * _Nonnull p) {
    *p = 42;                    // OK
}
```

### Option B: `nonnull` default (ergonomic)

```bash
clang -fnullability-safety -fnullability-default=nonnull -fsyntax-only file.c
```

Every unannotated pointer is non-null. You get warnings only where something is marked `_Nullable` or comes from a C library function that can return null (such as `malloc`). This is quiet, and suits new projects or codebases where most pointers shouldn't be null.

Mark pointers `_Nullable` where null is expected:

```c
// With -fnullability-default=nonnull
int * _Nullable find(int key);  // may return null

void caller(void) {
    int *result = find(42);
    *result = 0;                // warning: result may be null
    if (result) *result = 0;    // OK: checked
}
```

### Option C: annotations and pragmas only (most gradual)

```bash
clang -fnullability-safety -fsyntax-only file.c
```

With no `-fnullability-default`, a function is checked only if it has a `_Nullable` or `_Nonnull` annotation on a parameter or its return type, or if it is inside a `#pragma clang assume_nonnull` region. Everything else is untouched.

```c
// No annotation: not checked
void legacy(int *p) {
    *p = 42;  // no warning
}

// One annotation opts this function in
void checked(int * _Nullable p) {
    *p = 42;  // warning: p is _Nullable
}

// The pragma opts in a whole region; unannotated pointers inside it are _Nonnull
#pragma clang assume_nonnull begin

void also_checked(int *p) {
    *p = 42;  // OK: p is _Nonnull
}

#pragma clang assume_nonnull end
```

This lets you go one function or one file at a time: add a `_Nullable` to a function that crashes and it is fully checked, while the rest of the code stays silent.

## C library functions and annotated headers

The analysis already knows that `malloc`, `calloc`, `realloc`, `fopen`, `getenv`, `strchr`, `strstr`, and similar functions can return null, even with your system's headers. So this warns in Option A or B without anything extra:

```c
char *buf = malloc(100);
strcpy(buf, "hello");  // warning: passing nullable pointer to nonnull parameter
```

That warning also depends on `strcpy` declaring its parameters non-null, which glibc does. For a libc that doesn't, the fork ships annotated `stdlib.h`, `stdio.h`, and `string.h` in `clang/nullsafe-headers/include`:

```bash
clang -fnullability-safety -fnullability-default=nullable \
    -fsyntax-only -I/path/to/llvm-project/clang/nullsafe-headers/include file.c
```

## Flags reference

| Flag | Description |
|------|-------------|
| `-fsyntax-only` | Parse and type-check only; no object files. Use it to run the fork as a linter |
| `-fnullability-safety` | Enable the analysis (required) |
| `-fnullability-default=unspecified` | The default. Only annotated functions and `assume_nonnull` regions are checked |
| `-fnullability-default=nullable` | Unannotated pointers may be null. Most checking |
| `-fnullability-default=nonnull` | Unannotated pointers are non-null. Ergonomic mode |
| `-fno-nullability-stdlib-annotations` | Don't treat `malloc`, `fopen`, etc. as returning nullable |
| `-Werror=nullability-safety-dereference` | Make null dereference warnings errors |

## Editor integration

The release includes `clangd`, so warnings show up in your editor as you type. See [Editor integration](README.md#editor-integration) in the README.
