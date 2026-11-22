# Safe C annotations

Notes for a future clang project: making C and C++ interfaces say, in the
source and checked by the compiler, the facts memory safety depends on. Ideas
only so far; nothing is implemented here. Implementation would live on a branch
of `~/git/llvm-project`, next to the `nullability-safety` work.

These notes came out of building AbiGenie (`~/git/abigenie`), which had to
recover these facts from outside the headers for zstd, sqlite and hiredis.
Every safe binding needed answers to the same questions, and most C headers
answer none of them.

## The questions

For every pointer that crosses an interface (a parameter, a return value, a
struct field, a callback argument):

1. **Extent**: how many bytes are valid. Sized by a parameter, a field, a NUL
   terminator, or a constant.
2. **Direction**: read, written, or both.
3. **Lifetime**: borrowed for the call, kept by something else, handed over
   to the callee, or returned to the caller; and then who frees it, with what.
4. **Nullable** or not.
5. **Errors**: how failure is signalled (a predicate, a status code, NULL, a
   struct field) and where the message comes from.
6. **Callbacks**: how long the function pointer lives, plus these same
   questions for its own arguments.

For mechanical ports to safe Rust, two more:

7. **Aliasing**: can two pointers reach the same memory? Rust's `&mut` needs
   "no". C only has `restrict`, which is rare.
8. **Threading**: can the object move to another thread, or be shared across
   threads (Rust's `Send` / `Sync`)? C APIs say this only in prose.

## What clang already has

Checked 2026-11-22 with `probe-attributes.c` (`__has_attribute`) on Homebrew
clang 23 and Apple clang 21. "Recognized" means parsed, not necessarily
enforced everywhere.

| Question | Attributes | Gap |
|---|---|---|
| Extent | `counted_by`, `sized_by`, `counted_by_or_null` (both); `ended_by` (Apple only); `alloc_size`, `pass_object_size`, `nonstring` | Upstream applies `counted_by` to struct fields; parameters need Apple's `-fbounds-safety`, which is being upstreamed |
| Direction | only `const` | GCC's `access(read_only/write_only/read_write, ptr, size)` is not recognized by either clang. The clearest gap |
| Lifetime | `lifetimebound`, `lifetime_capture_by`, `noescape`, `ownership_returns/takes/holds`, `malloc`, `cf_returns_retained`, `cf_consumed`, `ns_returns_retained` | Several separate models (static analyzer, ARC/CF, lifetime warnings) that do not talk to each other; no allocator/deallocator pairing like GCC's `malloc(dealloc_fn)` |
| Nullable | `_Nonnull` / `_Nullable` / `_Null_unspecified`, `nonnull`, `returns_nonnull` | Mostly warnings; enforcement is what the `nullability-safety` fork works on |
| Errors | `warn_unused_result`, `swift_error`, `swift_async_error` | `swift_error` already encodes "NULL / nonzero / zero result means error", but only Swift reads it. Nothing for plain C callers |
| Callbacks | `callback` (which arguments are forwarded), `noescape` | No scope beyond "only during the call"; nothing for the callback's own arguments |
| Related | `format(printf, ...)`, `sentinel`, `enum_extensibility`, `flag_enum` | |

## The cross-cutting gap

Clang checks callers against these attributes, never implementations. Nothing
verifies that a function declared `counted_by(n)` stays within `n`, or that one
declared `ownership_takes` frees its argument. An annotation that the
definition is never held to is a promise, not a guarantee.

## Project ideas, roughly by payoff for effort

1. **GCC's `access` attribute in clang.** Direction and extent together, with
   syntax and semantics GCC already defined. Small, self-contained, upstreamable.
2. **`-fbounds-safety` on parameters upstream.** Help land what Apple has.
3. **Nullability enforcement as the model** for the other questions: the
   `nullability-safety` fork's approach applied to extent and lifetime.
4. **A C error-convention attribute.** "NULL means error", "negative means
   error", with a warning when a caller uses the value unchecked. `swift_error`
   is the starting point.
5. **One lifetime model** unifying `lifetimebound`, `lifetime_capture_by`,
   `ownership_*` and the CF/ARC attributes, plus allocator/deallocator pairing.
6. **Callback scope**: "kept until this object is freed", and annotations on
   the callback's own arguments.
7. **Definitions checked against declarations.** Make the body prove the
   interface: a per-function check, or a bounded model check (AbiGenie's
   ESBMC harness generator, `gen/src/prove.rs`, is a prototype of this).
8. **Aliasing and threading** annotations, for the port-to-Rust case.

## Why it matters beyond C

- **Mechanical ports to safe Rust.** c2rust produces `unsafe` Rust because C
  does not record these facts. With them, a translator can choose `&[u8]`,
  `Option<&T>`, owning types, lifetimes, `Result` and closures mechanically.
  The interface becomes mechanical; bodies still need idea 7.
- **Safe bindings in any language.** bindgen and similar tools could emit safe
  wrappers instead of raw ones from the same facts.
- **C++.** The attributes already work in C++, where `lifetimebound` is used
  most (catching a `string_view` that outlives its string). The same facts
  feed C++ safety profiles and C++-to-Rust ports.
- **The pitch to C programmers**: every one of these is an attribute their own
  compiler already parses. Nothing here asks anyone to rewrite in Rust.

## Open questions

- Can clang API notes (the YAML sidecar applied to existing headers, used for
  Swift and nullability) carry bounds and lifetime attributes? If so, existing
  headers could be annotated without editing them. Not yet checked.
- How much of Apple's `-fbounds-safety` is upstream today, exactly?
- Where should enforcement live for each question: Sema, the static analyzer,
  or a new pass?

## Files

- `probe-attributes.c`: prints which of these attributes a clang recognizes.
  `clang -w probe-attributes.c -o probe && ./probe`
