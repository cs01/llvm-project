# Nullsafe C: An experimental C/C++ compiler

[![Test Null-Safety](https://github.com/cs01/llvm-project/actions/workflows/test-null-safety.yml/badge.svg)](https://github.com/cs01/llvm-project/actions/workflows/test-null-safety.yml) &nbsp;&nbsp; [![Try Online](https://img.shields.io/badge/try-online-brightgreen)](https://cs01.github.io/llvm-project/)

> **Try it online:** [Interactive Playground](https://cs01.github.io/llvm-project/) - See null-safety warnings in real-time 

Nullsafe C adds NULL checks to catch errors at compile-time. It is 100% compatible with existing C codebases and can be used incrementally to identify safety issues at compile-time.

This provides the following benefits:
* Catches errors at compile-time rather than runtime, reducing crashes
* Improves developer experience by shifting errors left and showing issues in the IDE as you code
* Makes code more readable and maintainable, including annotations with `_Nonnull`
* Adds type checking that more modern languages have (Rust, TypeScript, Kotlin)

Nullsafe C analyzes pointer nullability using flow-sensitive analysis. By default, unannotated pointers have unspecified nullability (no warnings on existing code), but you can configure different defaults to suit your needs.

**Gradual Adoption:** Flow-sensitive analysis is only enabled for functions that have nullability annotations or are inside a `#pragma assume_nonnull` region. This allows you to migrate one function at a time - add annotations to critical functions first, then expand gradually.

It provides safety in two ways:

The first is by semantic analysis: if you test a pointer with `if(p)`, then it knows that branch contains a non-null pointer. This works for local variables, function parameters, and **struct member accesses** (e.g., `if (node->next)` narrows `node->next` to non-null).

The second is by using Clang's [`Nullability`](https://clang.llvm.org/docs/AttributeReference.html#nullability-attributes) attributes, in particular `_Nonnull`. If a pointer is marked as `_Nonnull` the compiler will require a pointer it knows it not null is passed to it. This can be done either by passing a `_Nonnull`-annotated pointer, or by doing type narrowing. 

If using a compiler other than clang, you can add `#define _Nonnull` as a no-op. You will not get the same compile checks as with Nullsafe C (clang fork), but the compilation will still succeed without error.

Note that this does not change any of the generated code. There are no performance impacts, it strictly does compile-time checks.

## Examples

With `-fflow-sensitive-nullability -fnullability-default=nullable`, unannotated pointers are treated as nullable:

```c
void unsafe(int *data) {
  *data = 42; // warning: dereferencing nullable pointer of type 'int * _Nullable'
}
```
*[Try it in the interactive playground](https://cs01.github.io/llvm-project/?code=dm9pZCB1bnNhZmUoaW50ICpkYXRhKSB7CiAgKmRhdGEgPSA0MjsgLy8gd2FybmluZzogZGVyZWZlcmVuY2luZyBudWxsYWJsZSBwb2ludGVyIG9mIHR5cGUgJ2ludCAqIF9OdWxsYWJsZScKfQ%3D%3D)*

Type narrowing:
```c
void safe(int *data) {
  if (data) {
    *data = 42; // OK - data is non-null here
  }
}
```
*[Try it in the interactive playground](https://cs01.github.io/llvm-project/?code=dm9pZCBzYWZlKGludCAqZGF0YSkgewogIGlmIChkYXRhKSB7CiAgICAqZGF0YSA9IDQyOyAvLyBPSyAtIGRhdGEgaXMgbm9uLW51bGwgaGVyZQogIH0KfQ%3D%3D)*

Annotated with `_Nonnull`:
```c
void safe_typed(int *_Nonnull data) {
  *data = 42; // OK - we know data is not null so we can dereference it
}
```
*[Try it in the interactive playground](https://cs01.github.io/llvm-project/?code=dm9pZCBzYWZlX3R5cGVkKGludCAqX05vbm51bGwgZGF0YSkgewogICpkYXRhID0gNDI7IC8vIE9LIC0gd2Uga25vdyBkYXRhIGlzIG5vdCBudWxsIHNvIHdlIGNhbiBkZXJlZmVybmNlIGl0Cn0%3D)*

Arrow operator (C++):
```cpp
struct Entity {
    int value() const { return 0; }
};

Entity* _Nullable getHead();

void buggy() {
    Entity* head = getHead();
    head->value();  // warning: dereferencing nullable pointer
}

void fixed() {
    Entity* head = getHead();
    if (!head) return;
    head->value();  // OK - narrowed to nonnull after check
}
```


## Installation

### Quick Install

```bash
curl -fsSL https://raw.githubusercontent.com/cs01/llvm-project/null-safe-c-dev/install.sh | bash
```

Or download manually from [releases](https://github.com/cs01/llvm-project/releases).

On mac you may need to do the following:
```bash
brew install zstd
xcode-select --install  # If not already installed
```

### Windows

Builds not available at this time, you must clone and build locally.

### What's Included

Each release includes:
- **`clang`** - The Null-Safe C compiler with flow-sensitive null checking
- **`clangd`** - Language server for IDE integration (VSCode, vim, Neovim, Emacs, etc.)

### IDE Integration

Once installed, configure your editor to use the null-safe `clangd`. Install the `clangd` extension from llvm and set the path to the clangd binary you just downloaded.

**VS Code:**
```json
// settings.json
{
  "clangd.path": "/path/to/null-safe-clang/bin/clangd"
}
```

**Neovim/vim:**
```lua
require('lspconfig').clangd.setup({
  cmd = { '/path/to/null-safe-clang/bin/clangd' }
})
```

This gives you real-time null-safety warnings as you type!

## Memory Safety

Note that this is not a comprehensive solution, since null pointer dereferences are just one category of memory safety bugs.

| Safety Issue            | Standard C | **Null-Safe Clang** (null checking) |
|-------------------------|------------|:-----------------------------------:|
| Null pointer dereferences | ❌ Unsafe | **✅ Fixed**                        |
| Buffer overflows        | ❌ Unsafe  | ❌ Unsafe                            |
| Use-after-free          | ❌ Unsafe  | ❌ Unsafe                            |
| Double-free             | ❌ Unsafe  | ❌ Unsafe                            |
| Uninitialized memory    | ❌ Unsafe  | ❌ Unsafe                            |

Although this doesn't fix all memory safety issues, it catches Null pointer dereferences for free. For additional memory safety, consider a language other than C. 

### Why you still might want to try this

While Null-Safe Clang doesn't solve all memory safety issues in C, null pointer dereferences are a significant problem:

- Many memory safety bugs involve null pointer dereferences
- Easier to adopt than rewriting in Rust (100% compatible with existing C code)
- Complements other efforts (combine with `-fbounds-safety` for buffer safety)
- Incremental deployment (warnings by default, can enable per-file)


## Usage

### Two-Flag System

Nullability checking is controlled by two independent flags:

**1. `-fflow-sensitive-nullability`** - Enables flow-sensitive nullability analysis
- Must be enabled to get any nullability checking
- When disabled, no nullability analysis is performed

**2. `-fnullability-default=<mode>`** - Sets the default nullability for unannotated pointers
- `unspecified` (default): No warnings for unannotated code. Flow analysis only activates inside `#pragma clang assume_nonnull` regions, ideal for gradual migration
- `nullable`: Unannotated pointers are nullable, forces null checks everywhere (defensive)
- `nonnull`: Unannotated pointers are non-null, flow analysis activates everywhere (ergonomic)

### Examples

```bash
# Large existing codebase - gradual migration (no warnings by default)
clang -fflow-sensitive-nullability -fnullability-default=unspecified mycode.c

# New defensive project - force null checks everywhere
clang -fflow-sensitive-nullability -fnullability-default=nullable mycode.c

# New ergonomic project - assume pointers are safe
clang -fflow-sensitive-nullability -fnullability-default=nonnull mycode.c

# With null-safe standard library headers
clang -fflow-sensitive-nullability -fnullability-default=nullable \
     -I/path/to/clang/nullsafe-headers/include mycode.c

# Treat nullability issues as errors
clang -fflow-sensitive-nullability -fnullability-default=nullable \
     -Werror=nullability mycode.c
```


## Features

- Configurable defaults: Choose between `unspecified` (gradual migration), `nullable` (defensive), or `nonnull` (ergonomic)
- Flow-sensitive narrowing: `if (p)` proves `p` is non-null in that scope
- Arrow operator checking: `p->member` warns if `p` is nullable (not just `*p`)
- Early-exit patterns: Understands `return`, `goto`, `break`, `continue`
- Pointer arithmetic: `q = p + 1` preserves narrowing from `p`
- Type checking through function calls, returns, and assignments
- Works with Typedefs
- Pragma support: `#pragma clang assume_nonnull begin/end` for API boundaries (function signatures only, not local variables)
- Function calls preserve narrowing: Since functions receive a copy of pointer arguments, they cannot modify the original pointer variable
- Null-safe headers: Annotated C standard library in `clang/nullsafe-headers/`
- IDE integration: `clangd` built from this fork has the same logic and warnings as clang

## Known Limitations

### Struct Member Narrowing

**✅ SUPPORTED** - Flow-sensitive narrowing now works for struct member accesses!

```c
struct Node {
    struct Node * _Nullable next;
};

void process(struct Node * _Nonnull node);

void example(struct Node *_Nonnull node) {
    if (node->next) {
        process(node->next);  // ✓ OK - node->next is narrowed to nonnull
    }
}
```

**How it works:** The analyzer tracks struct member nullability through control flow by maintaining a separate narrowing map for `(base_variable, field)` pairs. When you check `node->next`, it narrows that specific member access.

**Limitations:**
- Only works when the base variable is a local variable or parameter
- Pointer aliasing isn't tracked (modifying `*p` won't be detected if `p` aliases another tracked member)

**This is safe** because the base variable cannot change between the check and use within the same scope.


## Null-Safe C Standard Library

Nullability-annotated headers for `string.h`, `stdlib.h`, and `stdio.h` are available in `clang/nullsafe-headers/`. See [`clang/nullsafe-headers/README.md`](clang/nullsafe-headers/README.md) for details.

## Real-World Examples

Several popular C projects have been annotated with null-safety to demonstrate gradual migration:

### cJSON (In Progress)
A null-safe version of the [cJSON library](https://github.com/DaveGamble/cJSON) is available at [https://github.com/cs01/cJSON](https://github.com/cs01/cJSON) (branch `cs01/nullsafe`).

The migration uses `#pragma clang assume_nonnull` regions for API boundaries (function signatures).

Compile with null checking:
```bash
git clone -b cs01/nullsafe https://github.com/cs01/cJSON
cd cJSON
clang -fflow-sensitive-nullability -fnullability-default=unspecified \
     -I/path/to/clang/nullsafe-headers/include -fsyntax-only cJSON.c
```

**Note:** Uses `-fnullability-default=unspecified` mode because the file has `#pragma clang assume_nonnull` regions. The pragma only affects function parameters/returns (API boundaries), not local variables, enabling gradual adoption.

The migration demonstrates:
- File-scoped `#pragma clang assume_nonnull begin/end` for API defaults
- Explicit `_Nullable` annotations where pointers can be null (struct members, function pointers)
- Compatibility with existing C89 code
- Zero runtime overhead
- **Real bugs found**: The null-safety checker identifies ~20 potential null pointer dereferences

**Current limitations:**
- Some errors involve struct member accesses (e.g., `item->child` which is `_Nullable`) being passed to functions expecting nonnull
- Due to the [struct member narrowing limitation](#struct-member-narrowing), these require the local variable workaround
- This is representative of real-world codebases during gradual migration

**Understanding the warnings:**
- `-Wnullability-completeness`: Warns about pointers without explicit nullability inside `#pragma assume_nonnull` regions. These should be annotated with `_Nullable` or `_Nonnull` to be explicit.
- `-Wnullability`: Actual null-safety violations (dereferencing nullable pointers, passing nullable to nonnull, etc.). **These are potential bugs!**

Suppress completeness warnings if needed (after reviewing them):
```bash
clang -Wno-nullability-completeness ...
```

### SQLite and Redis (Coming Soon)
Additional real-world projects are being evaluated for gradual migration strategies.

See [`GRADUAL_MIGRATION.md`](GRADUAL_MIGRATION.md) for detailed migration strategies and best practices.
