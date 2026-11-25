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
| A field or global pointer is `_Owned`, `_Static` or `_Unchecked`; a struct of borrows is `_View` | Own |
| What is freed or given away is a whole block this code owns, on every path | - |
| Giving away moves: the old name is not used again; `_Take` moves out of a place | Own |
| `_Drop` runs once, on a value that is not given away by name | Drop |

## 5. A borrow ends before what it borrows

| Rule | Lean |
|---|---|
| A borrow of a block ends when the block is freed, and of a local when its scope ends | Own |
| A borrow of an owned field ends when the field is taken or replaced, or its object is lent exclusively | Own |
| A result borrows its one borrowed parameter, or the `_Lifetimebound` one; through a function pointer, all of them | - |
| An out argument may hold what the other arguments carry; `_Noescape` ones are not kept | Out |
| Arguments of one call do not overlap what it is lent exclusively or given | Out (one level) |
| A pointer to a place that holds a borrow names that place where it is used | - |
| A borrow of a global's owned memory ends with any call that may touch globals, and does not leave the file | Out (no files) |

## 6. Files agree

| Rule | Lean |
|---|---|
| The annotations of a shared function or global are the same in every file, or the program does not link | - |
| A function pointer's type carries the function's promises | - |

## 7. Threads

| Rule | Lean |
|---|---|
| A thread is given an owner, static data or null | - |
| A mutable global is guarded, atomic, thread-local, or `_Unsynchronized` | - |
| Guarded data is reached only with its lock, and no pointer to it is kept | - |
| A signal handler touches only what it may | - |

Not covered: a counted pointer and its count under one lock, shared owners, deadlock.

## Lists

<!-- generated:rules -->
**Statements and expressions** (72): `NullStmt`, `CompoundStmt`, `DeclStmt`, `IfStmt`, `SwitchStmt`, `WhileStmt`, `DoStmt`, `ForStmt`, `GotoStmt`, `ReturnStmt`, `CaseStmt`, `DefaultStmt`, `ContinueStmt`, `BreakStmt`, `LabelStmt`, `AttributedStmt`, `PredefinedExpr`, `DeclRefExpr`, `IntegerLiteral`, `FloatingLiteral`, `ImaginaryLiteral`, `StringLiteral`, `CharacterLiteral`, `ParenExpr`, `UnaryOperator`, `OffsetOfExpr`, `UnaryExprOrTypeTraitExpr`, `ArraySubscriptExpr`, `CallExpr`, `MemberExpr`, `BinaryOperator`, `CompoundAssignOperator`, `ConditionalOperator`, `BinaryConditionalOperator`, `ImplicitCastExpr`, `CStyleCastExpr`, `CompoundLiteralExpr`, `InitListExpr`, `DesignatedInitExpr`, `DesignatedInitUpdateExpr`, `ImplicitValueInitExpr`, `NoInitExpr`, `ParenListExpr`, `GenericSelectionExpr`, `SourceLocExpr`, `ConstantExpr`, `AtomicExpr`, `StmtExpr`, `ChooseExpr`, `GNUNullExpr`, `CXXNullPtrLiteralExpr`, `CXXBoolLiteralExpr`, `TypeTraitExpr`, `ExprWithCleanups`, `OpaqueValueExpr`, `RecoveryExpr`, `EmbedExpr`, `ShuffleVectorExpr`, `MaterializeTemporaryExpr`, `ConvertVectorExpr`, `GCCAsmStmt`, `VAArgExpr`, `BlockExpr`, `AssumptionExpr`, `BoundsSafetyPointerPromotionExpr`, `ForgePtrExpr`, `GetBoundExpr`, `PredefinedBoundsCheckExpr`, `BoundsCheckExpr`, `MaterializeSequenceExpr`, `TerminatedByToIndexableExpr`, `TerminatedByFromIndexableExpr`

**Casts** (33): `BitCast`, `LValueToRValue`, `NoOp`, `ArrayToPointerDecay`, `FunctionToPointerDecay`, `NullToPointer`, `IntegralToPointer`, `PointerToIntegral`, `PointerToBoolean`, `ToVoid`, `VectorSplat`, `IntegralCast`, `IntegralToBoolean`, `IntegralToFloating`, `FloatingToIntegral`, `FloatingToBoolean`, `BooleanToSignedIntegral`, `FloatingCast`, `FloatingRealToComplex`, `FloatingComplexToReal`, `FloatingComplexToBoolean`, `FloatingComplexCast`, `FloatingComplexToIntegralComplex`, `IntegralRealToComplex`, `IntegralComplexToReal`, `IntegralComplexToBoolean`, `IntegralComplexCast`, `IntegralComplexToFloatingComplex`, `AtomicToNonAtomic`, `NonAtomicToAtomic`, `BuiltinFnToFnPtr`, `BoundsSafetyPointerCast`, `Dependent`

**Declaration attributes** (63): `Annotate`, `AnnotateType`, `Unused`, `Used`, `Retain`, `Deprecated`, `Unavailable`, `Availability`, `Format`, `FormatArg`, `NonNull`, `NoReturn`, `AnalyzerNoReturn`, `Pure`, `Const`, `WarnUnusedResult`, `Restrict`, `AllocSize`, `Ownership`, `LifetimeBound`, `NoEscape`, `Capability`, `ScopedLockable`, `GuardedBy`, `PtGuardedBy`, `AcquireCapability`, `ReleaseCapability`, `TryAcquireCapability`, `RequiresCapability`, `LocksExcluded`, `AcquiredBefore`, `AcquiredAfter`, `Cleanup`, `Aligned`, `Packed`, `Visibility`, `AlwaysInline`, `NoInline`, `Cold`, `Hot`, `GNUInline`, `Flatten`, `MinSize`, `Leaf`, `NoThrow`, `NoDebug`, `Constructor`, `Destructor`, `EnumExtensibility`, `FlagEnum`, `Overloadable`, `Suppress`, `NoSanitize`, `Uninitialized`, `FallThrough`, `VectorSize`, `ExtVectorType`, `Mode`, `CountedBy`, `CountedByOrNull`, `SizedBy`, `SizedByOrNull`, `CDecl`

**Type attributes** (24): `PtrBidiIndexable`, `PtrIndexable`, `PtrSingle`, `PtrUnsafeIndexable`, `PtrTerminatedBy`, `PtrEndedBy`, `CountedBy`, `CountedByOrNull`, `SizedBy`, `SizedByOrNull`, `TypeNonNull`, `TypeNullable`, `TypeNullableResult`, `TypeNullUnspecified`, `AnnotateType`, `LifetimeBound`, `NoEscape`, `VectorSize`, `ExtVectorType`, `NoReturn`, `MayAlias`, `Mode`, `CDecl`, `NoThrow`

**Errors** (101; the bounds, null and lifetime errors of Clang itself are not listed):

| Rule | Message |
|---|---|
| `alloc-pointers` | the memory %0 returns is not zeroed, and %1 holds pointers, which would be garbage; use 'calloc' (or 'malloc', which Safe C zeroes) |
| `alloc-untracked` | a new allocation stored in %0 is not tracked, so nothing stops a use after it is freed; declare the place '_Owned' (for an array: 'T *_Owned a[N]'), or keep the pointer in a local variable |
| `atomic-cast` | a pointer to an '_Atomic' object used as a plain pointer makes unsynchronized accesses; keep the '_Atomic' type |
| `atomic-member` | a field of an '_Atomic' struct is accessed without synchronization; load or store the whole struct |
| `attr-not-in-safe-c` | Safe C has no rules for the attribute '%0', which may change what the compiler believes; remove it |
| `block` | blocks are not checked: what one captures may be gone when it runs; use a function and pass it what it needs |
| `borrow-slot` | %select{local\|field}0 %1 points to a place that holds a borrow, so a store through it changes what that place borrows without naming it; %select{write '&place' where it is used instead of keeping it\|pass the place's address as its own parameter}0 |
| `borrow-slot-value` | this gives a pointer to a place that holds a borrow or an owner, but which place cannot be told from here; name it directly ('&place', or a parameter) in each branch |
| `builtin-unchecked` | %0 is a compiler builtin that accesses memory without any check; call the library function it stands for ('memcpy', 'strlcpy', 'snprintf', or '<stdatomic.h>' operations on an '_Atomic' object) |
| `builtin-unchecked-name` | '%0' is a compiler builtin that accesses memory without any check; use '<stdatomic.h>' operations on an '_Atomic' object |
| `cast-align` | %0 needs stricter alignment than %1 has, and the address may not have it; 'memcpy' the bytes into a variable of type %0 |
| `compound-literal-store` | this compound literal's storage ends with the block it is written in, and the pointer assigned here may outlive it; declare it as a named variable in the block where the pointer is used (as in 'int vals[] = {1, 2};'), or initialize a new pointer with it |
| `const-search` | %0 drops the 'const' of what it searches, so this could write memory that must not change; keep the result in a 'const' pointer |
| `drop-moved-by-name` | %0 is freed automatically at the end of its scope ('_Drop'), so giving it away here would free it twice; move it out with '_Take(%1)', which leaves null for the cleanup |
| `drop-return` | %0 is cleaned up as this function returns, so the copy returned would hold freed memory; return a variable without '_Drop' |
| `drop-store` | %0 is freed automatically when its scope ends, so it may only hold memory it owns; store an allocation, '_Take(...)' of an owner, or null, not a pointer borrowed from a local or a parameter |
| `drop-type` | '_Drop_free' frees what %0 holds, so %0 must be a pointer to memory from the allocator; use '_Drop(function)' with the function that closes it |
| `excl-aliased-arg` | %0 is lent exclusively to this call, so no other argument may reach it (the callee could free or replace what that argument points to); pass it once, or finish with the other use before the call |
| `excl-arg-borrowed` | %0 is a shared borrow, but this parameter is '_Exclusive': the callee may free or replace what it owns while the owner still uses it; pass the owner, or declare %0 '_Owned' (if it owns the object) or '_Exclusive' |
| `excl-reassigned` | %0 is an exclusive borrow of the caller's object, which the caller still owns after the call; %select{assigning a new pointer to it\|giving it away}1 would leave the caller with the wrong object; use a local variable instead |
| `extern-local` | declare %0 at file scope (in a header), where its type and whether it may be shared are checked |
| `field-borrow-ended` | this pointer is borrowed from '%0', and the borrow ends (the field is taken, replaced, or lent to an exclusive parameter) before its last use; read '%0' again after that point, or finish using the pointer first |
| `fn-library-pointer` | %0 is checked only when called by name; call it inside a function of your own and use that one's address |
| `fn-promise` | %0 of this function has different ownership or bounds annotations than in %1, so a call through the pointer would not be checked the same way; write the function's annotations in the pointer's type |
| `free-borrowed-take` | %0 is borrowed, so its owner will free what it points to too; declare it '_Owned' if this code is responsible for freeing it |
| `free-handle` | a %0 is closed, not freed, and only its own library makes one; use 'fclose', 'pclose' or 'closedir' |
| `free-in-place` | freeing a pointer that is still stored in %0 leaves it dangling; move it out first, as in 'free(_Take(n->next))', where the place is '_Owned' |
| `frees-param` | %0 frees its parameter %1, but callers are not told and may use it afterwards; mark the parameter '_Owned' (it then takes ownership, like a Rust 'Box<T>' parameter) |
| `give-not-owner` | this is freed or given away here, so it must be a whole block this code owns (a new allocation, an owned parameter, or '_Take' of an owned field) on every path; it may not be an array, a borrow, or a pointer into the middle of a block: keep the original pointer and give that away |
| `global-borrow-returned` | this returns a borrow of memory a global owns, and callers in other files cannot see that, so they could use it after the global frees it; take the object that owns it as a parameter, return a copy ('_Returns_owned'), or make the function 'static' |
| `global-borrow-stored` | this borrows memory a global owns, and storing it where %0 points hides that from the caller, who could use it after the global frees it; take the object that owns it as a parameter instead of reading the global |
| `global-lent` | global %0 owns memory, and this call is passed a pointer to it but may also reach %0 by name and free what the callee is using; keep the object in a local for the call (move its owned fields with '_Take', and move them back after), or let the callee use the global by name |
| `global-unshared` | %0 is a mutable global, so every thread shares it and two threads can write it at once; make it '_Thread_local' (one copy per thread, the usual fix for single-threaded code), '_Atomic', guarded by a lock ('_Guarded_by(m)'), or 'const' if it never changes; if it is written only before any thread starts, mark it '_Unsynchronized' |
| `guarded-address` | %0 is guarded by a lock, and a pointer to it kept here could be used without the lock; pass '&%0' directly to the function that needs it |
| `heap-slot-borrow` | this heap memory holds pointers of type %0, which nothing tracks, so they can outlive what they point to; give the inner pointer a kind, as in 'T *_Owned *' if the memory owns what they point to, or 'T *_Static *' |
| `helper-place` | which object this call's result reaches cannot be told, so what borrows it would not end; name the object directly |
| `library-field` | the fields of %0 belong to the library; use its functions |
| `library-global-write` | %0 belongs to the C library and every thread shares it, unguarded; use the library's functions that do not need it |
| `library-unchecked` | nothing says how much memory %0 accesses or keeps, so this call cannot be checked; use a checked function ('snprintf', 'fread', 'strtol'), or write one wrapper for it that checks its sizes, with the call itself in '__unsafe' |
| `literal-move` | moving %0 into a literal or nested initializer by name leaves %0 still pointing at it; write '_Take(%0)' |
| `nested-count` | %0 holds the count of another field, so replacing it whole would change that count unseen; assign its fields one by one |
| `no-bounds-checks-needs-unsafe` | %0 is '_No_bounds_checks', which compiles it without run-time bounds checks; that is a promise only an audited function can make, so mark it '__unsafe' too (and say in a comment why every access is in bounds) |
| `noescape-stored` | %0 is '_Noescape', so callers count on it not being kept, but it is stored where %1 points; drop '_Noescape' from %0, and callers will keep what it borrows alive as long as they use %1 |
| `not-in-safe-c` | Safe C has no rules for this ('%0'), so nothing would check it; write it with ordinary statements, expressions and casts |
| `owned-in-union` | %0 is an '_Owned' member of an untagged union, so another member could read and free the same pointer; tag the union ('_Tagged_by', '_When'), or keep the owner outside it |
| `owned-param-stored` | %0 is owned by this function, which frees it or gives it on, but a borrow of it is stored where %1 points, so the caller could use it after that; return it instead, or store it as an owner ('_Owned *' parameter) |
| `owned-store` | %0 is '_Owned', so it must be given a pointer it may free: a new allocation, an owned local (which is then moved), or '_Take(...)' of another owned field; this value is borrowed, so its owner would free it too |
| `owned-store-unnamed` | this slot is '_Owned', so it must be given a pointer it may free: a new allocation, an owned local (which is then moved), or '_Take(...)' of another owned field |
| `ownership-through-borrow` | %select{taking from\|replacing}0 an owned field through '%1' changes what its owner holds while callers may still be using it; declare '%1' '_Exclusive' (an exclusive borrow, like Rust's '&mut')%select{, or '_Owned' if this function owns it\|}2 |
| `owning-struct-byte-copy` | copying %0 byte by byte duplicates its owned field %1, and both copies would free it; move the field with '_Take' and assign the others, or pass a pointer to the struct instead |
| `owning-struct-copy` | copying %0 duplicates its owned field %1, and both copies would free it; pass a pointer to it instead, or move the field out with '_Take' |
| `param-escape` | a borrow of %0 is stored where %1 points, which the caller can still read after %0 is gone; store something the caller lends or owns (a field of %1, or another parameter's data), or return the value instead; if a call here received it along with %1 and does not keep it, mark that function's parameter '_Noescape' |
| `partial-pointer` | %0 holds pointers, so it is copied or cleared only whole: the size must be 'n * sizeof' of it |
| `pragma-not-in-safe-c` | Safe C has no rules for '#pragma %0', which may change what names or memory mean; remove it |
| `printf-n` | '%%n' stores through a pointer nothing checks; use the value 'printf' returns |
| `printf-star` | nothing checks that this buffer holds as many characters as '%%.*s' is told to print; print it with 'fwrite(p, 1, n, file)', or pass a terminated string ('__null_terminated') |
| `realloc-pointers` | %0 holds pointers, and the memory 'realloc' adds is not zeroed, so its pointers would be garbage; 'calloc' the larger array and move the elements over ('_Take' each owned pointer) |
| `reborrow-ended` | %0 returns a borrow of '%1', which ends (an owned field of it is taken or replaced, or it is lent exclusively or given away) before this pointer's last use; call %0 again after that point, or finish using the pointer first |
| `restrict` | 'restrict' promises that nothing else reaches this memory, and nothing checks it; leave it out |
| `return-needs-lifetimebound` | %0 returns a pointer borrowed from %1, so it is valid only as long as what %1 points to; mark the parameter '_Lifetimebound' so callers keep it alive, or return an owned copy |
| `returns-fresh` | %0 returns memory it allocated, but callers are not told, so a use after they free it goes unchecked; declare it '__attribute__((ownership_returns(malloc)))' (safec.h: '_Returns_owned') |
| `scanf-arg` | scanf stores a whole value here unchecked; pass '&variable' of exactly the conversion's type, with no cast, that cannot be null |
| `scanf-size` | this conversion can store %0 characters here, so the argument must be an array of at least that size; lower the width, or enlarge the array |
| `signal-handler-global` | %0 is a signal handler, so it can run in the middle of any code, including code that is using %1; a handler may only touch globals that are '_Atomic', 'volatile sig_atomic_t' or 'const' (set a flag here and do the work in the main loop) |
| `sort-size` | the element size must be the size of %0 (write it with 'sizeof'): with any other size the sort would mix or overrun its elements |
| `spawn-borrow` | the new thread may outlive what this pointer borrows (the spawning function can return or free it first); give the thread memory it owns (a new allocation, moved in) or static data |
| `static-element` | these elements are '_Static', so they may point only at string literals, globals, functions or constants |
| `static-store` | %0 is '_Static', so it may point only at string literals, globals or functions, which are never freed; this value may point at memory that is; make %0 '_Owned' if it is responsible for freeing what it points to |
| `storage-borrow` | %select{field\|global}0 %1 holds a pointer nothing tracks, so it can outlive what it points to; declare it '_Owned' if it is responsible for freeing its target, or '_Static' if it only points at string literals, globals or functions%select{, or mark the struct '_View' if it lives only in locals and parameters\|}0 |
| `switch-not-exhaustive` | this switch on '%0' does not handle %1, a tag value of the union it describes; add 'case %1:' (or 'default:') |
| `tag-address` | %0 is a tag: changing it through a pointer would leave the union holding another member's bytes; assign it by name, together with the member |
| `tag-field-missing` | the union is tagged by '%0', but the struct has no field of that name; name the field that holds the tag, as in '_Tagged_by(kind)' |
| `tag-field-type` | the tag '%0' must be an integer or enum field, so its value can say which member is live |
| `tag-init-unknown` | the tag %0 is known only at run time, so the member initialized beside it may be the wrong one; set them together in a branch for each value |
| `tag-unpaired` | after this the tag %0 says one member is live while the union still holds another's bytes; assign a constant tag, and make the very next statement write the member it selects, in the same object |
| `tagged-member-address` | a pointer to %0 would outlive a change of the tag '%1', after which these bytes may be another member's (one that holds pointers); use the member by name, or copy its value out ('x = s.%2;') |
| `take-nonnull` | '_Take' leaves null behind, so '%0' must be declared '_Nullable' |
| `take-operand` | '_Take' moves a pointer out of a field or global and leaves null behind, so it needs a pointer that can be assigned, such as '_Take(n->next)' |
| `unchecked-read` | %0 is '_Unchecked': nothing tracks whether its target is still alive, so its value can be used only inside '__unsafe' (or copied into another '_Unchecked'); if this struct owns the target, make %0 '_Owned' instead |
| `union-init-mismatch` | this initializer sets the tag to %0 but fills %1, which is not live for that tag; initialize the member the tag names |
| `union-member-unchecked` | %select{reading\|writing}0 %1 needs the tag '%2' known to be %3 here, or the union's bytes could be another member's; check it first (as in %select{'if (x.%2 == %3)' or 'case %3:'\|'if (x.%2)'\|'if (!x.%2)'}4)%select{\|, or set it ('x.%2 = %3;') before writing}0 |
| `union-member-untracked` | %0 is reached through something whose tag cannot be followed here (an array element or a call result); take a pointer to the struct first ('struct T *e = &a[i];') and check 'e''s tag before using 'e->...' |
| `union-member-unwritten` | the tag '%1' now names %0, but %0 has not been written since the tag changed, so the union still holds another member's bytes; write %0 first |
| `unsynchronized-write` | %0 is '_Unsynchronized': it is shared without a lock, so a write is safe only before any thread starts; put this write inside '__unsafe { }' to say it is one of those |
| `used-after-drop` | this memory was freed when the '_Drop' variable holding it went out of scope; keep the variable alive as long as the copy, or move the pointer out of it with '_Take' to keep the memory |
| `used-after-spawn` | this memory was handed to a new thread, which may be using it right now; using it here too is a data race; give the thread its own copy, or share it through a lock |
| `valist` | nothing checks these arguments against the format; write one function marked 'format(printf, ...)' that passes its own format and '...' on once, between 'va_start' and 'va_end' |
| `vector-index` | the lane index of a vector is not checked; use a constant, or store the vector into an array first |
| `view-memcpy` | copying bytes into '_View' structs through a pointer hides which borrows they hold; assign each one through the local '_Owned' pointer that holds them ('v[i] = other') |
| `view-of-owner` | '_View' field %0 points to a struct that owns memory, and a view does not track what it could free; pass that struct as its own parameter |
| `view-pointer-storage` | %select{global\|field}0 %1 points to '_View' structs, whose borrows are tracked only while a local or parameter holds the pointer; keep it in a local and pass it as its own parameter |
| `view-realloc` | reallocating memory that holds '_View' structs loses track of what they borrow; allocate the new array and copy the views into it one by one |
| `view-storage` | %select{global\|field}0 %1 holds a '_View' struct, whose borrows are tracked only in locals and parameters; make it a local, or pass it as a parameter |
| `view-write-through-pointer` | writing a borrow into a '_View' through a pointer can outlive what it points to, because the view's own lifetime is unknown here; write to a local view by name ('v = ...' or a field of 'v'), or write through the local '_Owned' pointer that holds the views ('struct %0 *_Owned v = calloc(n, sizeof *v)'), whose borrows are tracked |
| `vla-param` | a parameter whose row length is another parameter is not checked; pass one row at a time, or a flat array with '__counted_by(rows * cols)' |
| `vla-pointers` | a variable-length array is not zeroed, and %0 holds pointers, which would be garbage; use a fixed size, or 'calloc' |
| `void-over-pointers` | %0 holds pointers, and as a 'void *' kept here its bytes could be overwritten (memcpy, memset, read), forging them; keep the pointer's own type, use 'const void *' if it is only read, or pass it directly in the call that needs 'void *' |
| `when-duplicate` | tag value %0 is claimed by both %1 and %2; each value must name one live member |
| `when-missing` | member %0 of a tagged union does not say when it is live; add '_When(VALUE)' with the tag values for which it holds data |
| `wide-copy-unchecked` | %0 can write past the end of its destination, which nothing checks; use the size-bounded form, as in 'wcslcat(dst, src, N)' with N the destination's length in wide characters |
| `wide-format` | %0 is not checked: nothing compares its arguments with its format (wide formats have no checking in the compiler); format with 'snprintf' and convert, or pass the text to 'fputws' |
<!-- end generated:rules -->
