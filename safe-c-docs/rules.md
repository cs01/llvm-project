# The rules

This is the one place that says what Safe C accepts. It has two kinds of content: the
rules in words (edit them here, together with the compiler), and lists generated from the
compiler's source, which cannot drift from it.

**How to read "Lean":** the model that covers the rule, in
[safe-c-lean](../safe-c-lean/). A dash means no model covers it. The models are small,
separate, and leave a lot out; nothing proves that Clang implements them.

Code outside these rules is in `__unsafe`, in a header marked legacy, in a system header,
or in a directory passed with `-isystem`. All of that is trusted.

## 0. What is in the language

Every kind of statement, expression, cast, attribute and pragma is on a list below. One
that is not is an error, even if it is harmless: a kind is added together with the rules
that cover it. Library functions are called only where their header, or the argument,
shows what they access.

## 1. Memory has a size

| Rule | Lean |
|---|---|
| A pointer is to one object, or says its count; locals carry their bounds | core |
| Every access is proven in bounds or checked, rows of 2-D arrays included | core |
| A count changes only together with its pointer | - |
| A string is terminated; a buffer passed as one is scanned within its bounds | - |

## 2. Memory is set before it is read

| Rule | Lean |
|---|---|
| A local is set on every path before it is read | - |
| `malloc` memory starts zeroed; memory that does not (`realloc`, a VLA) holds no pointers | core (zeroed heap) |
| A non-null pointer read from a field, element or global that was never set traps | - |

## 3. A pointer is what its type says

| Rule | Lean |
|---|---|
| Casts go to `void *`, to the same type, or between types with no pointers and no stricter alignment | core |
| Bytes never become pointers: not through `void *`, `memcpy`, `read`, unions or varargs | core (casts) |
| A union with pointers is tagged; a member is used only where its tag is known; tag and member are written together | Tag |
| Nullable pointers are checked before use | core |

## 4. Every block has one owner

| Rule | Lean |
|---|---|
| A field pointer is `_Owned`, `_Static`, `_Unchecked` or a function; a struct of borrows is a `_View` | Core (owners) |
| A global holds no owner and no borrow | - |
| An owner is moved by use; `_Take` moves one out of a field, leaving null | Core |
| A function that takes an owner always takes it | Core |
| `free` and the like take an owner; `realloc` takes it even when it fails | Core |
| Memory that will hold owners starts zeroed | Core |

## 5. A borrow ends before what it borrows

| Rule | Lean |
|---|---|
| Any pointer that is not an owner is a borrow, in a local or parameter only | Core |
| Changing what an object owns ends every other borrow with the same root, except its ancestors | Core |
| Nothing can be changed through `const T *` that T owns | Core |
| Two arguments do not reach the same owner if the call may change one | Core |
| A returned borrow comes from the first borrowed parameter | Core |
| A pointer to a local does not outlive its block | - |
| A `_View` is as if each of its pointers were a variable; behind a pointer it changes as `x = f(x)` | Core, CoreView (shape only) |
| A call through a function pointer is a call to any function with its marks | CoreIndirect (lemma) |
| An `_Unchecked` pointer is followed only in a `_Trusted` function | - |
| A pointer a library returns until the next call (`getenv`) ends at that call | - |

## 6. Files agree

| Rule | Lean |
|---|---|
| The annotations of a shared function or global are the same in every file, or the program does not link | - |
| A function pointer's type carries the function's promises | - |

## 7. Threads

| Rule | Lean |
|---|---|
| Guarded data is reached only with its lock, and no pointer to it is kept | - |
| A signal handler calls nothing but `write` and `_exit` | - |

Not covered: every other data race.

## Lists

<!-- generated:rules -->
**Statements and expressions** (72): `NullStmt`, `CompoundStmt`, `DeclStmt`, `IfStmt`, `SwitchStmt`, `WhileStmt`, `DoStmt`, `ForStmt`, `GotoStmt`, `ReturnStmt`, `CaseStmt`, `DefaultStmt`, `ContinueStmt`, `BreakStmt`, `LabelStmt`, `AttributedStmt`, `PredefinedExpr`, `DeclRefExpr`, `IntegerLiteral`, `FloatingLiteral`, `ImaginaryLiteral`, `StringLiteral`, `CharacterLiteral`, `ParenExpr`, `UnaryOperator`, `OffsetOfExpr`, `UnaryExprOrTypeTraitExpr`, `ArraySubscriptExpr`, `CallExpr`, `MemberExpr`, `BinaryOperator`, `CompoundAssignOperator`, `ConditionalOperator`, `BinaryConditionalOperator`, `ImplicitCastExpr`, `CStyleCastExpr`, `CompoundLiteralExpr`, `InitListExpr`, `DesignatedInitExpr`, `DesignatedInitUpdateExpr`, `ImplicitValueInitExpr`, `NoInitExpr`, `ParenListExpr`, `GenericSelectionExpr`, `SourceLocExpr`, `ConstantExpr`, `AtomicExpr`, `StmtExpr`, `ChooseExpr`, `GNUNullExpr`, `CXXNullPtrLiteralExpr`, `CXXBoolLiteralExpr`, `TypeTraitExpr`, `ExprWithCleanups`, `OpaqueValueExpr`, `RecoveryExpr`, `EmbedExpr`, `ShuffleVectorExpr`, `MaterializeTemporaryExpr`, `ConvertVectorExpr`, `GCCAsmStmt`, `VAArgExpr`, `BlockExpr`, `AssumptionExpr`, `BoundsSafetyPointerPromotionExpr`, `ForgePtrExpr`, `GetBoundExpr`, `PredefinedBoundsCheckExpr`, `BoundsCheckExpr`, `MaterializeSequenceExpr`, `TerminatedByToIndexableExpr`, `TerminatedByFromIndexableExpr`

**Casts** (33): `BitCast`, `LValueToRValue`, `NoOp`, `ArrayToPointerDecay`, `FunctionToPointerDecay`, `NullToPointer`, `IntegralToPointer`, `PointerToIntegral`, `PointerToBoolean`, `ToVoid`, `VectorSplat`, `IntegralCast`, `IntegralToBoolean`, `IntegralToFloating`, `FloatingToIntegral`, `FloatingToBoolean`, `BooleanToSignedIntegral`, `FloatingCast`, `FloatingRealToComplex`, `FloatingComplexToReal`, `FloatingComplexToBoolean`, `FloatingComplexCast`, `FloatingComplexToIntegralComplex`, `IntegralRealToComplex`, `IntegralComplexToReal`, `IntegralComplexToBoolean`, `IntegralComplexCast`, `IntegralComplexToFloatingComplex`, `AtomicToNonAtomic`, `NonAtomicToAtomic`, `BuiltinFnToFnPtr`, `BoundsSafetyPointerCast`, `Dependent`

**Declarations** (18): `TranslationUnit`, `Function`, `Var`, `ParmVar`, `ImplicitParam`, `Field`, `IndirectField`, `Record`, `Enum`, `EnumConstant`, `Typedef`, `Label`, `Empty`, `StaticAssert`, `PragmaComment`, `PragmaDetectMismatch`, `FileScopeAsm`, `Block`

**Types** (14): `Builtin`, `Record`, `Enum`, `BitInt`, `Vector`, `ExtVector`, `FunctionNoProto`, `Complex`, `Pointer`, `Atomic`, `ConstantArray`, `IncompleteArray`, `VariableArray`, `FunctionProto`

**Declaration attributes** (62): `Annotate`, `AnnotateType`, `Unused`, `Used`, `Retain`, `Deprecated`, `Unavailable`, `Availability`, `Format`, `NonNull`, `NoReturn`, `AnalyzerNoReturn`, `Pure`, `Const`, `WarnUnusedResult`, `Restrict`, `AllocSize`, `Ownership`, `LifetimeBound`, `NoEscape`, `Capability`, `ScopedLockable`, `GuardedBy`, `PtGuardedBy`, `AcquireCapability`, `ReleaseCapability`, `TryAcquireCapability`, `RequiresCapability`, `LocksExcluded`, `AcquiredBefore`, `AcquiredAfter`, `Cleanup`, `Aligned`, `Packed`, `Visibility`, `AlwaysInline`, `NoInline`, `Cold`, `Hot`, `GNUInline`, `Flatten`, `MinSize`, `Leaf`, `NoThrow`, `NoDebug`, `Constructor`, `Destructor`, `EnumExtensibility`, `FlagEnum`, `Overloadable`, `Suppress`, `NoSanitize`, `Uninitialized`, `FallThrough`, `VectorSize`, `ExtVectorType`, `Mode`, `CountedBy`, `CountedByOrNull`, `SizedBy`, `SizedByOrNull`, `CDecl`

**Type attributes** (24): `PtrBidiIndexable`, `PtrIndexable`, `PtrSingle`, `PtrUnsafeIndexable`, `PtrTerminatedBy`, `PtrEndedBy`, `CountedBy`, `CountedByOrNull`, `SizedBy`, `SizedByOrNull`, `TypeNonNull`, `TypeNullable`, `TypeNullableResult`, `TypeNullUnspecified`, `AnnotateType`, `LifetimeBound`, `NoEscape`, `VectorSize`, `ExtVectorType`, `NoReturn`, `MayAlias`, `Mode`, `CDecl`, `NoThrow`

**Errors** (78; the bounds, null and lifetime errors of Clang itself are not listed):

| Rule | Message |
|---|---|
| `alloc-pointers` | the memory %0 returns is not zeroed, and %1 holds pointers, which would be garbage; use 'calloc' (or 'malloc', which Safe C zeroes) |
| `alloca` | 'alloca' is checked only with a constant size and in a function with no variable-length array; use a fixed-size array, or 'malloc' and 'free' |
| `atomic-cast` | a pointer to an '_Atomic' object used as a plain pointer makes unsynchronized accesses; keep the '_Atomic' type |
| `atomic-member` | a field of an '_Atomic' struct is accessed without synchronization; load or store the whole struct |
| `atomic-pointer` | this pointer's bounds are not checked by an atomic operation; pass '&variable' or '&s.field', or store it in a '__single' pointer first |
| `attr-not-in-safe-c` | Safe C has no rules for the attribute '%0', which may change what the compiler believes; remove it |
| `block` | blocks are not checked: what one captures may be gone when it runs; use a function and pass it what it needs |
| `builtin-unchecked` | %0 is a compiler builtin that accesses memory without any check; call the library function it stands for ('memcpy', 'strlcpy', 'snprintf', or '<stdatomic.h>' operations on an '_Atomic' object) |
| `builtin-unchecked-name` | '%0' is a compiler builtin that accesses memory without any check; use '<stdatomic.h>' operations on an '_Atomic' object |
| `cast-align` | %0 needs stricter alignment than %1 has, and the address may not have it; 'memcpy' the bytes into a variable of type %0 |
| `cast-incomplete` | %0 is not complete here, so nothing says what the memory holds; move the cast after the struct's definition |
| `compound-literal-store` | this compound literal's storage ends with the block it is written in, and the pointer assigned here may outlive it; declare it as a named variable in the block where the pointer is used (as in 'int vals[] = {1, 2};'), or initialize a new pointer with it |
| `const-search` | %0 drops the 'const' of what it searches, so this could write memory that must not change; keep the result in a 'const' pointer |
| `count-address` | %0 is the count of another field, and a write through its address would not be checked; use a local variable and assign '%0' from it |
| `count-behind` | a count behind a pointer can be changed through another name; pass the count by value and return the new one |
| `count-group-base` | a pointer and its count are set together, through a plain variable ('p->n', 's.n'); make a pointer to the element first |
| `count-group-between` | a pointer and its count are set together, with nothing between them; move this before or after them, or use '_Set' |
| `flex-bytes` | %0 keeps the length of its own array, so writing its bytes can make the length wrong; assign its fields by name |
| `flex-cast` | nothing says the memory behind this pointer has the array that %0 ends with, or its length; allocate a %0 and fill it field by field |
| `flex-nested` | %0 ends in an array whose length is a field, and inside another struct that length is not checked; keep a pointer to it |
| `fn-library-pointer` | %0 is checked only when called by name; call it inside a function of your own and use that one's address |
| `fn-promise` | %0 of this function has different ownership or bounds annotations than in %1, so a call through the pointer would not be checked the same way; write the function's annotations in the pointer's type |
| `format-pointer` | only a function can check its arguments against a format, and this is not one; call the function by name |
| `free-handle` | a %0 is closed, not freed, and only its own library makes one; use 'fclose', 'pclose' or 'closedir' |
| `guarded-address` | %0 is guarded by a lock, and a pointer to it kept here could be used without the lock; pass '&%0' directly to the function that needs it |
| `init-override` | this replaces part of a value given whole earlier in the same braces, and the rest may depend on it; assign it after the declaration |
| `library-field` | the fields of %0 belong to the library; use its functions |
| `library-unchecked` | nothing says how much memory %0 accesses or keeps, so this call cannot be checked; use a checked function ('snprintf', 'fread', 'strtol'), or write one wrapper for it that checks its sizes, with the call itself in '__unsafe' |
| `main-count` | the only count 'main' is given is 'argc' |
| `nested-count` | %0 holds the count of another field, so replacing it whole would change that count unseen; assign its fields one by one |
| `no-bounds-checks-needs-unsafe` | %0 is '_No_bounds_checks', which compiles it without run-time bounds checks; that is a promise only an audited function can make, so mark it '__unsafe' too (and say in a comment why every access is in bounds) |
| `not-in-safe-c` | Safe C has no rules for this ('%0'), so nothing would check it; write it with ordinary statements, expressions and casts |
| `order` | C does not say which side of this runs first, and one reads a pointer from memory that the other may change or free; do it in two statements |
| `out-arg` | %0 writes through this pointer unchecked; pass '&variable' of a plain local variable and copy the value where it belongs after the call |
| `owned-in-union` | %0 is an '_Owned' member of an untagged union, so another member could read and free the same pointer; tag the union ('_Tagged_by', '_When'), or keep the owner outside it |
| `partial-pointer` | %0 holds pointers, so it is copied or cleared only whole: the size must be 'n * sizeof' of it |
| `pragma-not-in-safe-c` | Safe C has no rules for '#pragma %0', which may change what names or memory mean; remove it |
| `printf-format` | the format is a string literal, so that what it prints can be checked; write it in the call ('"%%s"' prints a string) |
| `printf-n` | '%%n' stores through a pointer nothing checks; use the value 'printf' returns |
| `printf-single` | '%%s' reads until a zero byte, and nothing says this pointer has one; declare it '__null_terminated' (a 'const char *' already is) |
| `printf-star` | nothing checks that this buffer holds as many characters as '%%.*s' is told to print; print it with 'fwrite(p, 1, n, file)', or pass a terminated string ('__null_terminated') |
| `realloc-pointers` | %0 holds pointers, and the memory 'realloc' adds is not zeroed, so its pointers would be garbage; 'calloc' the larger array and move the elements over ('_Take' each owned pointer) |
| `redeclared` | %0 is a library function; include its header, whose declaration says what it accesses |
| `restrict` | 'restrict' promises that nothing else reaches this memory, and nothing checks it; leave it out |
| `scanf-arg` | scanf stores a whole value here unchecked; pass '&variable' of exactly the conversion's type, with no cast, that cannot be null |
| `scanf-plain` | scanf writes whatever the address is; pass '&' of a local variable that is not a count, or the name of a local array, or read a line with 'fgets' and convert it ('strtol') |
| `scanf-size` | this conversion can store %0 characters here, so the argument must be an array of at least that size; lower the width, or enlarge the array |
| `sort-size` | the element size must be the size of %0 (write it with 'sizeof'): with any other size the sort would mix or overrun its elements |
| `string-alias` | this buffer is checked for its terminator before the call, and %0 could change it after that; pass a string ('__null_terminated'), or a local array that no other argument can reach |
| `string-result` | the result is a string inside a buffer that can still be changed; search a string ('__null_terminated'), or use 'str_find' (text.h) |
| `switch-not-exhaustive` | this switch on '%0' does not handle %1, a tag value of the union it describes; add 'case %1:' (or 'default:') |
| `tag-address` | %0 is a tag: changing it through a pointer would leave the union holding another member's bytes; assign it by name, together with the member |
| `tag-field-missing` | the union is tagged by '%0', but the struct has no field of that name; name the field that holds the tag, as in '_Tagged_by(kind)' |
| `tag-field-type` | the tag '%0' must be an integer or enum field, so its value can say which member is live |
| `tag-init-unknown` | the tag %0 is known only at run time, so the member initialized beside it may be the wrong one; set them together in a branch for each value |
| `tag-unpaired` | after this the tag %0 says one member is live while the union still holds another's bytes; assign a constant tag, and make the very next statement write the member it selects, in the same object |
| `tagged-member-address` | a pointer to %0 would outlive a change of the tag '%1', after which these bytes may be another member's (one that holds pointers); use the member by name, or copy its value out ('x = s.%2;') |
| `tagged-pointers` | a tag is not checked for a union that holds pointers; use a struct with one field per member and leave the others null |
| `take-nonnull` | '_Take' leaves null behind, so '%0' must be declared '_Nullable' |
| `take-operand` | '_Take' moves a pointer out of a field or global and leaves null behind, so it needs a pointer that can be assigned, such as '_Take(n->next)' |
| `take-shared-count` | the count of %0 is also the count of another pointer, which '_Take' would change; give each its own count |
| `terminator-step` | this may overwrite the terminator of a string; assign a character instead |
| `type-not-in-safe-c` | Safe C has no rules for this kind of type ('%0') |
| `union-init-mismatch` | this initializer sets the tag to %0 but fills %1, which is not live for that tag; initialize the member the tag names |
| `union-member-unchecked` | %select{reading\|writing}0 %1 needs the tag '%2' known to be %3 here, or the union's bytes could be another member's; check it first (as in %select{'if (x.%2 == %3)' or 'case %3:'\|'if (x.%2)'\|'if (!x.%2)'}4)%select{\|, or set it ('x.%2 = %3;') before writing}0 |
| `union-member-untracked` | %0 is reached through something whose tag cannot be followed here (an array element or a call result); take a pointer to the struct first ('struct T *e = &a[i];') and check 'e''s tag before using 'e->...' |
| `union-member-unwritten` | the tag '%1' now names %0, but %0 has not been written since the tag changed, so the union still holds another member's bytes; write %0 first |
| `unsafe-function` | '%0' %1; use %2 |
| `valist` | nothing checks these arguments against the format; write one function marked 'format(printf, ...)' that passes its own format and '...' on once, between 'va_start' and 'va_end' |
| `vector-index` | the lane index of a vector is not checked; use a constant, or store the vector into an array first |
| `vla-param` | a parameter whose row length is another parameter is not checked; pass one row at a time, or a flat array with '__counted_by(rows * cols)' |
| `vla-pointer` | the length in this pointer's type (%0) is not checked against what it points to; use a pointer to the element with '__counted_by(n)' |
| `vla-pointers` | a variable-length array is not zeroed, and %0 holds pointers, which would be garbage; use a fixed size, or 'calloc' |
| `void-over-pointers` | %0 holds pointers, and as a 'void *' kept here its bytes could be overwritten (memcpy, memset, read), forging them; keep the pointer's own type, use 'const void *' if it is only read, or pass it directly in the call that needs 'void *' |
| `when-duplicate` | tag value %0 is claimed by both %1 and %2; each value must name one live member |
| `when-missing` | member %0 of a tagged union does not say when it is live; add '_When(VALUE)' with the tag values for which it holds data |
| `wide-copy-unchecked` | %0 can write past the end of its destination, which nothing checks; use the size-bounded form, as in 'wcslcat(dst, src, N)' with N the destination's length in wide characters |
| `wide-format` | %0 is not checked: nothing compares its arguments with its format (wide formats have no checking in the compiler); format with 'snprintf' and convert, or pass the text to 'fputws' |
<!-- end generated:rules -->
