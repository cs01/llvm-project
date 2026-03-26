# Nullsafe Clang Standard Library Headers

Nullability-annotated C standard library headers for use with Nullsafe Clanglang. Uses Clang's `_Nonnull` and `_Nullable` attributes to catch null pointer dereferences at compile time.

## Quick Start

```c
#include "string.h"
#include "stdlib.h"

void example(const char* input) {
    if (!input) return;

    char* copy = malloc(strlen(input) + 1);
    if (copy) {
        strcpy(copy, input);
        free(copy);
    }
}
```

Compile:
```bash
clang -fflow-sensitive-nullability -fnullability-default=nullable -Iclang/nullsafe-headers/include mycode.c
```

## What's Included

- `string.h` - String manipulation (strlen, strcpy, memcpy, etc.)
- `stdlib.h` - Memory allocation, conversions (malloc, atoi, etc.)
- `stdio.h` - File I/O (fopen, printf, fgets, etc.)

## Examples

### Memory Allocation

```c
void process_data(size_t size) {
    char* buffer = malloc(size);  // Returns _Nullable

    // ERROR - buffer might be NULL
    buffer[0] = 'x';

    // CORRECT - check first
    if (buffer) {
        buffer[0] = 'x';
        free(buffer);  // free() accepts _Nullable
    }
}
```

### String Operations

```c
void safe_copy(char* dest, const char* src) {
    // ERROR - both might be NULL, but strcpy requires _Nonnull
    strcpy(dest, src);

    // CORRECT
    if (dest && src) {
        strcpy(dest, src);
    }
}
```

### File I/O

```c
void read_config(const char* filename) {
    FILE* fp = fopen(filename, "r");  // Returns _Nullable

    // ERROR - fp might be NULL
    char buf[100];
    fgets(buf, sizeof(buf), fp);

    // CORRECT
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            // Process line
        }
        fclose(fp);
    }
}
```

## FAQ

**Q: Does this require Nullsafe Clanglang?**
A: No. Works with any Clang. Nullsafe Clanglang provides better flow-sensitive analysis.

**Q: Do I need to recompile libc?**
A: No. These are just declarations; links to system libc.

**Q: What about third-party libraries?**
A: Use `-isystem` to suppress warnings:
```bash
clang -Inullsafe-headers/include -isystem /usr/include/python3.9 mycode.c
```

**Q: Performance overhead?**
A: Zero. All checks are compile-time only.

## Contributing

Guidelines for adding annotations:
1. Mark `_Nonnull` only when NULL causes undefined behavior
2. Mark `_Nullable` when NULL is explicitly allowed as input
3. Return types default to `_Nullable` (conservative)
4. Test against real libc to verify correctness

See the [main README](../../README.md) for more information about Nullsafe Clanglang.

