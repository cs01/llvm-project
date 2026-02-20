# Gradual Migration Guide for Nullsafe C

This guide shows how to incrementally adopt null-safety checking in existing codebases, from massive legacy projects to greenfield code.

## ✨ NEW: Function-Level Gradual Adoption

As of the latest version, **flow-sensitive analysis is only enabled for functions with nullability annotations**. This makes adoption even more gradual:

```c
// Unannotated function - NO flow checking, no warnings
void legacy_code(int *p) {
    *p = 42;  // No warning - unannotated code is unchanged
}

// Annotated function - FULL flow checking enabled
void modern_code(int *_Nullable p) {
    if (p) {
        *p = 42;  // ✓ OK - proven safe by flow analysis
    }
    *p = 0;  // ERROR - might be null here
}

// Inside pragma - flow checking enabled for entire region
#pragma clang assume_nonnull begin

void all_code_here_is_checked(int *p) {
    *p = 42;  // Checked - p is _Nonnull by pragma
}

#pragma clang assume_nonnull end
```

**Why this is powerful:**
- **One function at a time**: Annotate ONE function parameter → entire function gets flow checking
- **Immediate value**: Each annotation enables hundreds of safety checks
- **Zero disruption**: Unannotated code continues working exactly as before
- **TypeScript-style**: Same gradual typing model that conquered JavaScript

This means you can:
1. Enable `-fflow-sensitive-nullability` project-wide immediately
2. Add `_Nullable` to crash-prone functions one at a time
3. Each annotation provides instant safety checking with zero impact on other code

## Table of Contents
- [Migration Strategy](#migration-strategy)
- [Compiler Flags Reference](#compiler-flags-reference)
- [Migration Workflows](#migration-workflows)
- [Using Null-Safe Standard Library](#using-null-safe-standard-library)
- [Best Practices](#best-practices)

---

## Migration Strategy

### Three-Phase Approach (Recommended for Large Codebases)

Based on production experience from teams at Google and other organizations, here's the proven approach:

#### Phase 1: Enable Analysis, Add Explicit Annotations
**Goal**: Get initial null-safety checking without breaking the build

```bash
# Enable flow-sensitive checking with unknown default
clang -fflow-sensitive-nullability -fnullability-default=unspecified mycode.c
```

**What to do**:
1. Turn on the checker across your codebase
2. Add explicit `_Nullable` annotations where pointers are known to be possibly null
3. Add explicit `_Nonnull` annotations to high-value APIs (frequently-called functions with parameters that must be non-null)
4. Most pointers remain unannotated (Unknown) - no warnings yet

**Benefits at this stage**:
- Get some initial safety checking on annotated code
- No build breakage - unannotated code produces no warnings
- Teams can learn the system incrementally

#### Phase 2: File-by-File Migration to Default Nonnull
**Goal**: Make nullability explicit for entire files

For each file you want to fully migrate:

```c
#pragma clang assume_nonnull begin

// Your file contents here
// Unannotated pointers are now _Nonnull by default

void my_function(
    int* data,              // _Nonnull (implicit)
    char* _Nullable name    // _Nullable (explicit - can be NULL)
) {
    *data = 42;  // Safe - data is nonnull
    if (name) {
        // Check before using nullable pointer
    }
}

#pragma clang assume_nonnull end
```

**What to do**:
1. Add `#pragma clang assume_nonnull begin/end` around file contents
2. Remove any explicit `_Nonnull` annotations (they're now the default)
3. Keep explicit `_Nullable` where pointers can be null
4. Add missing `_Nullable` annotations revealed by warnings
5. Fix any null-safety violations

**Key insight**: Some warnings at this stage indicate missing `_Nullable` from Phase 1, not bugs. Add the annotation rather than "fixing" the code.

#### Phase 3: Project-Wide Default Nonnull (Optional)
**Goal**: Make new code safe by default

Once most files are migrated:

```bash
# Change the default for the whole project
clang -fflow-sensitive-nullability -fnullability-default=nonnull mycode.c
```

**What to do**:
1. Remove file-level pragmas (now redundant)
2. New code is nonnull by default
3. Continue marking `_Nullable` where needed

---

## Compiler Flags Reference

### Core Nullability Flags

#### `-fflow-sensitive-nullability`
**Required** - Enables the entire null-safety checking system.

```bash
# Without this flag, no nullability analysis happens
clang -fflow-sensitive-nullability mycode.c
```

#### `-fnullability-default=<mode>`
Controls how unannotated pointers are interpreted. Choose based on your migration phase:

**`unspecified` (default)** - Best for Phase 1 (gradual migration)
```bash
clang -fflow-sensitive-nullability -fnullability-default=unspecified mycode.c
```
- Unannotated pointers have unspecified nullability
- No warnings on unannotated code
- Perfect for large existing codebases starting migration

**`nullable`** - Defensive/strict mode
```bash
clang -fflow-sensitive-nullability -fnullability-default=nullable mycode.c
```
- Unannotated pointers are treated as nullable
- Forces null checks everywhere
- Good for safety-critical code

**`nonnull`** - Ergonomic mode for new code
```bash
clang -fflow-sensitive-nullability -fnullability-default=nonnull mycode.c
```
- Unannotated pointers are treated as nonnull
- Clean, minimal annotations
- Best for Phase 3 or greenfield projects

**Important**: Local variables are always nullable by default, regardless of this setting (unless inside `#pragma clang assume_nonnull` region). This is intentional - locals can be reassigned to null at any time.

#### `-fstrict-nullability-inference` (default: enabled)
Treats flow-inferred nullability as if it were explicit.

```bash
# Disable for looser checking (not recommended)
clang -fflow-sensitive-nullability -fno-strict-nullability-inference mycode.c
```

Most users should leave this at the default (enabled).

### Standard Null-Related Flags

These are standard clang flags that work with or without Nullsafe C:

#### `-fdelete-null-pointer-checks` (default: enabled)
Tells the optimizer that dereferencing null is undefined behavior, enabling aggressive optimizations.

```bash
# Disable for more conservative optimizations
clang -fno-delete-null-pointer-checks mycode.c
```

#### `-fcheck-new` (C++ only)
Assume `operator new` might return null (normally it throws on failure).

#### `-fnew-infallible` / `-fno-new-infallible` (C++ only)
Control whether `operator new` is annotated with `returns_nonnull`.

---

## Migration Workflows

### Workflow 1: Massive Legacy Codebase

**Scenario**: Million+ lines of C, can't break the build

```bash
# Step 1: Enable with unknown default (no warnings)
CC="clang -fflow-sensitive-nullability -fnullability-default=unspecified"
make

# Step 2: Annotate high-value APIs in header files
# Add _Nonnull to frequently-called functions
# Add _Nullable where null is allowed

# Step 3: Migrate files one at a time
# Add #pragma clang assume_nonnull begin/end to each file
# Fix warnings as you go
```

**Timeline**: Months to years, but you get incremental value throughout.

### Workflow 2: Medium Codebase with Active Development

**Scenario**: 50k-500k lines, active development, want safety soon

```bash
# Step 1: Start with unknown, annotate critical APIs
clang -fflow-sensitive-nullability -fnullability-default=unspecified

# Step 2: Migrate new files with pragma
# All new code uses #pragma clang assume_nonnull

# Step 3: Gradually migrate old files
# One subsystem at a time

# Step 4: Eventually switch to nonnull default
clang -fflow-sensitive-nullability -fnullability-default=nonnull
```

**Timeline**: 3-12 months depending on team size.

### Workflow 3: New Project (Greenfield)

**Scenario**: Starting from scratch, want safety from day one

```bash
# Use nonnull default from the start
clang -fflow-sensitive-nullability -fnullability-default=nonnull

# Mark _Nullable explicitly where null is allowed
# Everything else is nonnull by default
```

**Example**:
```c
// Compiled with -fnullability-default=nonnull

void process_data(
    const char* filename,        // _Nonnull (implicit)
    char* _Nullable error_msg    // _Nullable (explicit)
) {
    // filename is guaranteed non-null
    FILE* fp = fopen(filename, "r");

    if (!fp) {
        if (error_msg) {
            strcpy(error_msg, "Failed to open file");
        }
        return;
    }

    // ... process file ...
}
```

### Workflow 4: Hybrid Approach (New Code Strict, Old Code Gradual)

**Scenario**: Want new code to be safe, but can't fix old code yet

Use per-file compilation flags or organize code by directory:

```bash
# Old code: unknown default
clang -fflow-sensitive-nullability -fnullability-default=unspecified legacy/*.c

# New code: nonnull default
clang -fflow-sensitive-nullability -fnullability-default=nonnull src/*.c
```

Or use pragmas in new files:
```c
// new_feature.c
#pragma clang assume_nonnull begin

// All pointers nonnull by default in this file

#pragma clang assume_nonnull end
```

---

## Using Null-Safe Standard Library

Nullsafe C includes nullability-annotated versions of common standard library headers in `clang/nullsafe-headers/`.

### What's Included

- `string.h` - String manipulation (strlen, strcpy, memcpy, etc.)
- `stdlib.h` - Memory allocation, conversions (malloc, atoi, etc.)
- `stdio.h` - File I/O (fopen, printf, fgets, etc.)

### How to Use

Add the nullsafe headers directory to your include path:

```bash
clang -fflow-sensitive-nullability -fnullability-default=nullable \
     -I/path/to/clang/nullsafe-headers/include \
     mycode.c
```

### Example

```c
#include "string.h"
#include "stdlib.h"

void process_string(const char* _Nullable input) {
    if (!input) return;

    // malloc returns _Nullable - must check
    char* copy = malloc(strlen(input) + 1);
    if (copy) {
        // strcpy requires both params to be _Nonnull
        // Flow analysis knows both are non-null here
        strcpy(copy, input);
        free(copy);  // free accepts _Nullable
    }
}
```

The annotated headers will catch common mistakes:

```c
char* str = malloc(100);
strcpy(str, "hello");  // WARNING: str might be NULL!
```

### Recommendations

1. **Start using annotated headers early** - Even in Phase 1, use the nullsafe headers to catch bugs in new code

2. **Suppress warnings from third-party headers** - Use `-isystem` for external libraries:
   ```bash
   clang -I/path/to/nullsafe-headers/include \
        -isystem /usr/include/python3.9 \
        mycode.c
   ```

3. **Zero runtime overhead** - All checks are compile-time only, no performance impact

4. **Works with any libc** - These are just declarations; links to your system's standard C library

---

## Best Practices

### Do's

✅ **Start with `-fnullability-default=unspecified`** for existing codebases
- No build breakage
- Incremental migration
- Immediate value from partial annotations

✅ **Use `#pragma clang assume_nonnull` for file-level defaults**
```c
#pragma clang assume_nonnull begin
// File contents
#pragma clang assume_nonnull end
```

✅ **Annotate public APIs first**
- Headers used across many files
- Frequently-called functions
- Maximum impact for effort

✅ **Use the nullsafe standard library headers**
- Catches bugs in existing code
- Free safety improvements

✅ **Check before dereferencing**
```c
void safe(int* _Nullable data) {
    if (data) {
        *data = 42;  // OK - narrowed to nonnull
    }
}
```

✅ **Use early returns for cleaner code**
```c
void process(char* _Nullable str) {
    if (!str) return;

    // Rest of function knows str is non-null
    *str = 'x';
}
```

### Don'ts

❌ **Don't start with `-fnullability-default=nonnull` on legacy code**
- Will produce thousands of warnings
- Team will be overwhelmed
- Start with `unspecified` instead

❌ **Don't try to migrate everything at once**
- File-by-file or subsystem-by-subsystem
- Incremental progress is better

❌ **Don't ignore warnings**
- Each warning is a potential null pointer crash
- Fix or annotate, don't suppress

❌ **Don't assume local variables follow `-fnullability-default`**
- Locals are always nullable unless in `#pragma clang assume_nonnull`
- This is intentional and good design

❌ **Don't pass unchecked nullable to nonnull**
```c
void needs_nonnull(int* _Nonnull p);

void bad(int* _Nullable p) {
    needs_nonnull(p);  // ERROR!
}

void good(int* _Nullable p) {
    if (p) {
        needs_nonnull(p);  // OK - checked first
    }
}
```

### Tips for Success

1. **Treat warnings as errors eventually**
   ```bash
   clang -fflow-sensitive-nullability -fnullability-default=nullable \
        -Werror=nullability mycode.c
   ```
   Start lenient, get stricter over time.

2. **Use IDE integration**
   - Configure `clangd` from the nullsafe-clang build
   - Get warnings as you type
   - Faster feedback loop

3. **Communicate the value**
   - Null pointer dereferences are ~10-20% of crashes in C programs
   - Compile-time checking vs runtime debugging
   - No performance overhead

4. **Measure progress**
   - Track % of files with pragmas
   - Track % of functions with explicit annotations
   - Celebrate milestones

5. **Learn from warnings**
   - Some warnings reveal actual bugs
   - Some reveal missing `_Nullable` annotations
   - Both improve code quality

---

## Summary

**For existing codebases**: Start with Phase 1 (`-fnullability-default=unspecified`), annotate incrementally, migrate file-by-file.

**For new projects**: Use Phase 3 (`-fnullability-default=nonnull`) from day one.

**Key insight**: The three-state system (unknown/nullable/nonnull) enables gradual migration. You get value immediately without breaking existing code.

**Remember**: Every `_Nonnull` annotation is a crash you've prevented. Every null check is a bug you've caught at compile-time instead of in production.
