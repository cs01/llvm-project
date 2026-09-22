# Annotated C Standard Library Headers

Replacement `stdlib.h`, `stdio.h`, and `string.h` whose declarations carry Clang's `_Nonnull` and `_Nullable` annotations. With [Nullability Safety](../../README.md) they let the compiler catch null pointer bugs around C library calls at compile time.

## Do you need them?

Often not. Without these headers the analysis already treats the results of C library functions that can return null (`malloc`, `calloc`, `realloc`, `fopen`, `getenv`, `strchr`, `strstr`, and others) as `_Nullable`. Parameter contracts come from your libc: glibc marks parameters like `strcpy`'s with `__attribute__((nonnull))`, and the analysis uses that.

These headers help when your libc doesn't declare those contracts, or when you want them spelled out as `_Nonnull`/`_Nullable` in one place.

## Quick start

```c
#include "string.h"
#include "stdlib.h"

void example(const char *input) {
    if (!input) return;

    char *copy = malloc(strlen(input) + 1);
    if (copy) {
        strcpy(copy, input);
        free(copy);
    }
}
```

```bash
clang -fnullability-safety -fnullability-default=nullable \
      -Iclang/nullsafe-headers/include mycode.c
```

## What's included

- `string.h`: string and memory functions (`strlen`, `strcpy`, `memcpy`, ...)
- `stdlib.h`: allocation and conversions (`malloc`, `free`, `atoi`, ...)
- `stdio.h`: file I/O (`fopen`, `printf`, `fgets`, ...)
- `nullsafe_stl.h`: C++ only. Documents which standard library methods the compiler already knows return non-null pointers (such as `std::vector::data()`). Those are built in and work without this header.

## Examples

The warnings below assume `-fnullability-safety -fnullability-default=nullable`.

### Memory allocation

```c
void process_data(size_t size) {
    char *buffer = malloc(size);  // returns _Nullable

    buffer[0] = 'x';              // warning: buffer may be null

    if (buffer) {
        buffer[0] = 'x';          // OK
        free(buffer);             // free() accepts _Nullable
    }
}
```

### String operations

```c
void copy(char *dest, const char *src) {
    strcpy(dest, src);            // warning: strcpy's parameters are _Nonnull

    if (dest && src) {
        strcpy(dest, src);        // OK
    }
}
```

### File I/O

```c
void read_config(const char * _Nonnull filename) {
    char buf[100];
    FILE *fp = fopen(filename, "r");  // returns _Nullable

    fgets(buf, sizeof(buf), fp);      // warning: fp may be null

    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            // process the line
        }
        fclose(fp);
    }
}
```

## FAQ

**Do these headers work with stock Clang?**
Yes. They are ordinary declarations. Stock Clang uses the annotations for its type-based `-Wnullability` checks (for example, passing a `_Nullable` value to a `_Nonnull` parameter), but it does not follow null checks or diagnose dereferences. That needs `-fnullability-safety`.

**Do I need to rebuild libc?**
No. These are declarations only; your program still links against the system libc.

**How do I keep warnings out of third-party headers?**
Include them with `-isystem` so Clang suppresses their warnings:
```bash
clang -Iclang/nullsafe-headers/include -isystem /usr/include/python3.9 mycode.c
```

**Is there a runtime cost?**
No. The annotations only affect compile-time checking.

## Contributing

When adding annotations:
1. Mark a parameter `_Nonnull` only if passing null is undefined behavior.
2. Mark a parameter `_Nullable` if the function documents that null is allowed.
3. Mark a return `_Nullable` unless the function can never return null.
4. Check each declaration against the real libc.
