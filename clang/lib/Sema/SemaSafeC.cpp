//===--- SemaSafeC.cpp - Safe C (-fsafe-c) regions and diagnostics -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// `__unsafe` region tracking, and Safe C diagnostics registered at run time.
// New Safe C errors go in the table below rather than in
// DiagnosticSemaKinds.td: editing the .td regenerates tables most of Sema
// includes, while this file is compiled on its own.
//
//===----------------------------------------------------------------------===//

#include "SafeCState.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/Analyses/MemberAccessPath.h"
#include "clang/Analysis/CFG.h"
#include "clang/AST/SafeCTypes.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/Path.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CommandLine.h"
#include <functional>

using namespace clang;

SafeCState &Sema::safeC() {
  if (!SafeC)
    SafeC = std::make_unique<SafeCState>();
  return *SafeC;
}

bool Sema::inSafeCUnsafeParse() const { return SafeC && SafeC->Depth > 0; }

bool Sema::isInSafeCUnsafeRegion(SourceLocation Loc) const {
  if (Loc.isInvalid() || !SafeC || SafeC->Regions.empty())
    return false;
  Loc = SourceMgr.getFileLoc(Loc);
  for (const SafeCState::Region &R : SafeC->Regions) {
    SourceLocation B = SourceMgr.getFileLoc(R.Range.getBegin());
    SourceLocation E = SourceMgr.getFileLoc(R.Range.getEnd());
    if (!SourceMgr.isBeforeInTranslationUnit(Loc, B) &&
        !SourceMgr.isBeforeInTranslationUnit(E, Loc))
      return true;
  }
  return false;
}

void Sema::beginSafeCUnsafeRegion(SourceLocation Begin, bool IsFunction) {
  SafeCState &St = safeC();
  ++St.Depth;
  St.Open.push_back(St.Regions.size());
  St.Regions.push_back({SourceRange(Begin, Begin), IsFunction});
}

void Sema::endSafeCUnsafeRegion(SourceLocation End) {
  SafeCState &St = safeC();
  if (St.Depth == 0 || St.Open.empty())
    return;
  --St.Depth;
  SafeCState::Region &R = St.Regions[St.Open.pop_back_val()];
  R.Range.setEnd(End.isValid() ? End : R.Range.getBegin());
}

void Sema::noteSafeCUnsafeRegionUsed(SourceLocation Loc) {
  SafeCState &St = safeC();
  if (!St.Open.empty()) {
    St.Regions[St.Open.back()].Used = true;
    return;
  }
  // Raised after parsing: the innermost closed region holding Loc is the
  // latest-starting one that contains it.
  Loc = SourceMgr.getFileLoc(Loc);
  SafeCState::Region *Best = nullptr;
  for (SafeCState::Region &R : St.Regions) {
    SourceLocation B = SourceMgr.getFileLoc(R.Range.getBegin());
    SourceLocation E = SourceMgr.getFileLoc(R.Range.getEnd());
    if (SourceMgr.isBeforeInTranslationUnit(Loc, B) ||
        SourceMgr.isBeforeInTranslationUnit(E, Loc))
      continue;
    if (!Best || SourceMgr.isBeforeInTranslationUnit(
                     SourceMgr.getFileLoc(Best->Range.getBegin()), B))
      Best = &R;
  }
  if (Best)
    Best->Used = true;
}

void Sema::emitSafeCUnusedUnsafeWarnings() {
  // An __unsafe region in which nothing was suppressed lifts checks for no
  // reason. Only regions spelled directly in the user's code are reported: one
  // inside another macro may be needed at its other uses. After an error,
  // some diagnostics were never issued, so say nothing.
  if (!getLangOpts().SafeC || !SafeC || getDiagnostics().hasErrorOccurred())
    return;
  for (const SafeCState::Region &R : SafeC->Regions) {
    SourceLocation B = R.Range.getBegin();
    if (R.Used || B.isInvalid() || SourceMgr.isInSystemHeader(B) ||
        (B.isMacroID() && SourceMgr.getImmediateMacroCallerLoc(B).isMacroID()))
      continue;
    Diag(B, diag::warn_safe_c_unused_unsafe) << R.IsFunction;
  }
}

namespace {
/// A Safe C diagnostic registered at run time. Errors are issued as errors no
/// flag or pragma can lower, and are ignored inside `__unsafe`; notes attach
/// to the preceding error.
struct SafeCDiagInfo {
  const char *Key;
  bool IsNote;
  const char *Text;
};

// New Safe C diagnostics go here. Say why the code is unsafe and what to
// write instead; never suggest __unsafe first.
const SafeCDiagInfo SafeCDiagTable[] = {
    {"take-operand", false,
     "'_Take' moves a pointer out of a field or global and leaves "
     "null behind, so it needs a pointer that can be assigned, such as "
     "'_Take(n->next)'"},
    {"take-nonnull", false,
     "'_Take' leaves null behind, so '%0' must be declared "
     "'_Nullable'"},
    {"no-bounds-checks-needs-unsafe", false,
     "%0 is '_No_bounds_checks', which compiles it without run-time bounds "
     "checks; that is a promise only an audited function can make, so mark "
     "it '__unsafe' too (and say in a comment why every access is in bounds)"},
    {"outparam-local-type", true,
     "the callee stores a pointer without bounds into %0, but a local "
     "pointer carries them by default; declare it as '%1;'"},
    {"tag-field-missing", false,
     "the union is tagged by '%0', but the struct has no field of that name; "
     "name the field that holds the tag, as in '_Tagged_by(kind)'"},
    {"tag-field-type", false,
     "the tag '%0' must be an integer or enum field, so its value can say "
     "which member is live"},
    {"when-missing", false,
     "member %0 of a tagged union does not say when it is live; add "
     "'_When(VALUE)' with the tag values for which it holds data"},
    {"when-duplicate", false,
     "tag value %0 is claimed by both %1 and %2; each value must name one "
     "live member"},
    {"switch-not-exhaustive", false,
     "this switch on '%0' does not handle %1, a tag value of the union it "
     "describes; add 'case %1:' (or 'default:')"},
    {"union-member-unchecked", false,
     "%select{reading|writing}0 %1 needs the tag '%2' known to be %3 here, "
     "or the union's bytes could be another member's; check it first (as in "
     "%select{'if (x.%2 == %3)' or 'case %3:'|'if (x.%2)'|'if (!x.%2)'}4)"
     "%select{|, or set it ('x.%2 = %3;') before writing}0"},
    {"tagged-member-address", false,
     "a pointer to %0 would outlive a change of the tag '%1', after which "
     "these bytes may be another member's (one that holds pointers); use the "
     "member by name, or copy its value out ('x = s.%2;')"},
    {"wide-copy-unchecked", false,
     "%0 can write past the end of its destination, which nothing checks; "
     "use the size-bounded form, as in 'wcslcat(dst, src, N)' with N the "
     "destination's length in wide characters"},
    {"union-member-untracked", false,
     "%0 is reached through something whose tag cannot be followed here (an "
     "array element or a call result); take a pointer to the struct first "
     "('struct T *e = &a[i];') and check 'e''s tag before using 'e->...'"},
    {"union-member-unwritten", false,
     "the tag '%1' now names %0, but %0 has not been written since the tag "
     "changed, so the union still holds another member's bytes; write %0 "
     "first"},
    {"union-init-mismatch", false,
     "this initializer sets the tag to %0 but fills %1, which is not live "
     "for that tag; initialize the member the tag names"},
    {"sort-size", false,
     "the element size must be the size of %0 (write it with 'sizeof'): with "
     "any other size the sort would mix or overrun its elements"},
    {"void-over-pointers", false,
     "%0 holds pointers, and as a 'void *' kept here its bytes could be "
     "overwritten (memcpy, memset, read), forging them; keep the pointer's "
     "own type, use 'const void *' if it is only read, or pass it "
     "directly in the call that needs 'void *'"},
    {"realloc-pointers", false,
     "%0 holds pointers, and the memory 'realloc' adds is not zeroed, so its "
     "pointers would be garbage; 'calloc' the larger array and move the "
     "elements over ('_Take' each owned pointer)"},
    {"alloc-pointers", false,
     "the memory %0 returns is not zeroed, and %1 holds pointers, which would "
     "be garbage; use 'calloc' (or 'malloc', which Safe C zeroes)"},
    {"printf-star", false,
     "nothing checks that this buffer holds as many characters as '%%.*s' is "
     "told to print; print it with 'fwrite(p, 1, n, file)', or pass a "
     "terminated string ('__null_terminated')"},
    {"scanf-size", false,
     "this conversion can store %0 characters here, so the argument must be an "
     "array of at least that size; lower the width, or enlarge the array"},
    {"scanf-arg", false,
     "scanf stores a whole value here unchecked; pass '&variable' of exactly "
     "the conversion's type, with no cast, that cannot be null"},
    {"builtin-unchecked", false,
     "%0 is a compiler builtin that accesses memory without any check; call "
     "the library function it stands for ('memcpy', 'strlcpy', 'snprintf', or "
     "'<stdatomic.h>' operations on an '_Atomic' object)"},
    {"free-handle", false,
     "a %0 is closed, not freed, and only its own library makes one; use "
     "'fclose', 'pclose' or 'closedir'"},
    {"fn-promise", false,
     "%0 of this function has different ownership or bounds annotations than "
     "in %1, so a call through the pointer would not be checked the same way; "
     "write the function's annotations in the pointer's type"},
    {"fn-library-pointer", false,
     "%0 is checked only when called by name; call it inside a function of "
     "your own and use that one's address"},
    {"block", false,
     "blocks are not checked: what one captures may be gone when it runs; use "
     "a function and pass it what it needs"},
    {"tag-address", false,
     "%0 is a tag: changing it through a pointer would leave the union holding "
     "another member's bytes; assign it by name, together with the member"},
    {"library-unchecked", false,
     "nothing says how much memory %0 accesses or keeps, so this call cannot "
     "be checked; use a checked function ('snprintf', 'fread', 'strtol'), or "
     "write one wrapper for it that checks its sizes, with the call itself in "
     "'__unsafe'"},
    {"builtin-unchecked-name", false,
     "'%0' is a compiler builtin that accesses memory without any check; use "
     "'<stdatomic.h>' operations on an '_Atomic' object"},
    {"tag-init-unknown", false,
     "the tag %0 is known only at run time, so the member initialized beside "
     "it may be the wrong one; set them together in a branch for each value"},
    {"tag-unpaired", false,
     "after this the tag %0 says one member is live while the union still "
     "holds another's bytes; assign a constant tag, and make the very next "
     "statement write the member it selects, in the same object"},
    {"printf-n", false,
     "'%%n' stores through a pointer nothing checks; use the value 'printf' "
     "returns"},
    {"valist", false,
     "nothing checks these arguments against the format; write one function "
     "marked 'format(printf, ...)' that passes its own format and '...' on "
     "once, between 'va_start' and 'va_end'"},
    {"tagged-pointers", false,
     "a tag is not checked for a union that holds pointers; use a struct with "
     "one field per member and leave the others null"},
    {"alloca", false,
     "'alloca' is checked only with a constant size and in a function with no "
     "variable-length array; use a fixed-size array, or 'malloc' and 'free'"},
    {"out-arg", false,
     "%0 writes through this pointer unchecked; pass '&variable' of a plain "
     "local variable and copy the value where it belongs after the call"},
    {"vla-pointer", false,
     "the length in this pointer's type (%0) is not checked against what it "
     "points to; use a pointer to the element with '__counted_by(n)'"},
    {"flex-bytes", false,
     "%0 keeps the length of its own array, so writing its bytes can make the "
     "length wrong; assign its fields by name"},
    {"unsafe-function", false, "'%0' %1; use %2"},
    {"order", false,
     "C does not say which side of this runs first, and one reads a pointer "
     "from memory that the other may change or free; do it in two statements"},
    {"count-group-base", false,
     "a pointer and its count are set together, through a plain variable "
     "('p->n', 's.n'); make a pointer to the element first"},
    {"count-group-between", false,
     "a pointer and its count are set together, with nothing between them; "
     "move this before or after them, or use '_Set'"},
    {"count-behind", false,
     "a count behind a pointer can be changed through another name; pass the "
     "count by value and return the new one"},
    {"take-shared-count", false,
     "the count of %0 is also the count of another pointer, which '_Take' would "
     "change; give each its own count"},
    {"main-count", false,
     "the only count 'main' is given is 'argc'"},
    {"printf-format", false,
     "the format is a string literal, so that what it prints can be checked; "
     "write it in the call ('\"%%s\"' prints a string)"},
    {"scanf-plain", false,
     "scanf writes whatever the address is; pass '&' of a local variable that "
     "is not a count, or the name of a local array, or read a line with "
     "'fgets' and convert it ('strtol')"},
    {"terminator-step", false,
     "this may overwrite the terminator of a string; assign a character "
     "instead"},
    {"string-alias", false,
     "this buffer is checked for its terminator before the call, and %0 could "
     "change it after that; pass a string ('__null_terminated'), or a local "
     "array that no other argument can reach"},
    {"string-result", false,
     "the result is a string inside a buffer that can still be changed; search "
     "a string ('__null_terminated'), or use 'str_find' (text.h)"},
    {"printf-single", false,
     "'%%s' reads until a zero byte, and nothing says this pointer has one; "
     "declare it '__null_terminated' (a 'const char *' already is)"},
    {"format-pointer", false,
     "only a function can check its arguments against a format, and this is "
     "not one; call the function by name"},
    {"range-designator", false,
     "'[a ... b] = x' runs 'x' once and copies the result into every "
     "element, so an owner or a call there would be shared by all of them; "
     "use a constant, or set the elements in a loop"},
    {"vla-pointers", false,
     "a variable-length array is not zeroed, and %0 holds pointers, which "
     "would be garbage; use a fixed size, or 'calloc'"},
    {"atomic-pointer", false,
     "this pointer's bounds are not checked by an atomic operation; pass "
     "'&variable' or '&s.field', or store it in a '__single' pointer first"},
    {"redeclared", false,
     "%0 is a library function; include its header, whose declaration says "
     "what it accesses"},
    {"cast-incomplete", false,
     "%0 is not complete here, so nothing says what the memory holds; move "
     "the cast after the struct's definition"},
    {"init-override", false,
     "this replaces part of a value given whole earlier in the same braces, "
     "and the rest may depend on it; assign it after the declaration"},
    {"count-address", false,
     "%0 is the count of another field, and a write through its address would "
     "not be checked; use a local variable and assign '%0' from it"},
    {"flex-cast", false,
     "nothing says the memory behind this pointer has the array that %0 ends "
     "with, or its length; allocate a %0 and fill it field by field"},
    {"flex-nested", false,
     "%0 ends in an array whose length is a field, and inside another struct "
     "that length is not checked; keep a pointer to it"},
    {"nested-count", false,
     "%0 holds the count of another field, so replacing it whole would change "
     "that count unseen; assign its fields one by one"},
    {"vla-param", false,
     "a parameter whose row length is another parameter is not checked; pass "
     "one row at a time, or a flat array with '__counted_by(rows * cols)'"},
    {"vector-index", false,
     "the lane index of a vector is not checked; use a constant, or store the "
     "vector into an array first"},
    {"restrict", false,
     "'restrict' promises that nothing else reaches this memory, and nothing "
     "checks it; leave it out"},
    {"atomic-member", false,
     "a field of an '_Atomic' struct is accessed without synchronization; load "
     "or store the whole struct"},
    {"cast-align", false,
     "%0 needs stricter alignment than %1 has, and the address may not have "
     "it; 'memcpy' the bytes into a variable of type %0"},
    {"not-in-safe-c", false,
     "Safe C has no rules for this ('%0'), so nothing would check it; write it "
     "with ordinary statements, expressions and casts"},
    {"pragma-not-in-safe-c", false,
     "Safe C has no rules for '#pragma %0', which may change what names or "
     "memory mean; remove it"},
    {"type-not-in-safe-c", false,
     "Safe C has no rules for this kind of type ('%0')"},
    {"attr-not-in-safe-c", false,
     "Safe C has no rules for the attribute '%0', which may change what the "
     "compiler believes; remove it"},
    {"partial-pointer", false,
     "%0 holds pointers, so it is copied or cleared only whole: the size must "
     "be 'n * sizeof' of it"},
    {"const-end", false,
     "%0 would set a pointer that can write to text that must not change; pass "
     "NULL for the end, and use 'str_int' (text.h) to know where a number ends"},
    {"const-search", false,
     "%0 drops the 'const' of what it searches, so this could write memory "
     "that must not change; keep the result in a 'const' pointer"},
    {"library-field", false,
     "the fields of %0 belong to the library; use its functions"},
    {"atomic-cast", false,
     "a pointer to an '_Atomic' object used as a plain pointer makes "
     "unsynchronized accesses; keep the '_Atomic' type"},
    {"guarded-address", false,
     "%0 is guarded by a lock, and a pointer to it kept here could be used "
     "without the lock; pass '&%0' directly to the function that needs it"},
    {"owned-in-union", false,
     "%0 is an '_Owned' member of an untagged union, so another member could "
     "read and free the same pointer; tag the union ('_Tagged_by', '_When'), "
     "or keep the owner outside it"},
    {"wide-format", false,
     "%0 is not checked: nothing compares its arguments with its format (wide "
     "formats have no checking in the compiler); format with 'snprintf' and "
     "convert, or pass the text to 'fputws'"},
    {"compound-literal-store", false,
     "this compound literal's storage ends with the block it is written in, "
     "and the pointer assigned here may outlive it; declare it as a named "
     "variable in the block where the pointer is used (as in "
     "'int vals[] = {1, 2};'), or initialize a new pointer with it"},
};
} // namespace

Sema::SemaDiagnosticBuilder Sema::SafeCDiag(SourceLocation Loc,
                                            StringRef Key) {
  static const llvm::StringMap<const SafeCDiagInfo *> Table = [] {
    llvm::StringMap<const SafeCDiagInfo *> M;
    for (const SafeCDiagInfo &D : SafeCDiagTable)
      M[D.Key] = &D;
    return M;
  }();
  auto It = Table.find(Key);
  assert(It != Table.end() && "unknown Safe C diagnostic key");
  const SafeCDiagInfo &D = *It->second;
  DiagnosticIDs &IDs = *getDiagnostics().getDiagnosticIDs();
  if (D.IsNote)
    return Diag(Loc, IDs.getCustomDiagID(DiagnosticIDs::CustomDiagDesc(
                         diag::Severity::Fatal, D.Text,
                         DiagnosticIDs::CLASS_NOTE)));
  bool InUnsafe = inSafeCUnsafeParse() || isInSafeCUnsafeRegion(Loc);
  if (InUnsafe)
    noteSafeCUnsafeRegionUsed(Loc);
  else
    ++NumSafeCSafetyErrors;
  return Diag(Loc, IDs.getCustomDiagID(DiagnosticIDs::CustomDiagDesc(
                       InUnsafe ? diag::Severity::Ignored
                                : diag::Severity::Error,
                       D.Text,
                       InUnsafe ? DiagnosticIDs::CLASS_WARNING
                                : DiagnosticIDs::CLASS_ERROR)));
}

namespace {
struct UnsafeFunction {
  StringRef Name, Why, Instead;
};
} // namespace
/// Library functions with no safe use, or none that can be checked. (Apple's
/// headers already name the replacements of gets, strcpy, strcat, sprintf...)
static const UnsafeFunction UnsafeFunctions[] = {
    {"strtok", "writes into the string and remembers it between calls",
     "'str_before' and 'str_after' (text.h), or 'strcspn' for where each "
     "word ends"},
    {"strtok_r", "writes into the string and remembers it between calls",
     "'str_before' and 'str_after' (text.h), or 'strcspn' for where each "
     "word ends"},
    {"strsep", "writes into the string and moves your pointer to it",
     "'str_before' and 'str_after' (text.h), or 'strcspn' for where each "
     "part ends"},
    {"realpath", "writes a path of any length into the buffer it is given",
     "'realpath(path, NULL)', and free the result"},
    {"getwd", "writes a path of any length", "'getcwd(buffer, sizeof buffer)'"},
    {"tmpnam", "names a file that another program may create first", "'mkstemp'"},
    {"tempnam", "names a file that another program may create first", "'mkstemp'"},
    {"mktemp", "names a file that another program may create first", "'mkstemp'"},
    {"asctime", "writes into one buffer the whole program shares", "'strftime'"},
    {"asctime_r", "writes a string of any length", "'strftime'"},
    {"ctime_r", "writes a string of any length", "'localtime_r' and 'strftime'"},
    {"putenv", "keeps the string it is given",
     "a small function marked '__unsafe' that calls 'setenv', before any "
     "'getenv'"},
    {"setenv", "may free what 'getenv' returned",
     "a small function marked '__unsafe' that calls it, before any 'getenv'"},
    {"unsetenv", "may free what 'getenv' returned",
     "a small function marked '__unsafe' that calls it, before any 'getenv'"},
    {"strxfrm", "returns a length that may be more than it wrote", "'strcoll'"},
    {"setbuf", "does not know how big the buffer is", "the buffer the stream has"},
    {"setvbuf", "keeps the buffer it is given", "the buffer the stream has"},
    {"setbuffer", "keeps the buffer it is given", "the buffer the stream has"},
    {"gethostbyname", "returns memory the next call changes", "a small function marked '__unsafe' that calls 'getaddrinfo'"},
    {"gethostbyaddr", "returns memory the next call changes", "a small function marked '__unsafe' that calls 'getnameinfo'"},
    {"inet_ntoa", "returns memory the next call changes", "a small function marked '__unsafe' that calls 'inet_ntop'"},
    {"getlogin", "returns memory the next call changes", "'getlogin_r'"},
    {"ttyname", "returns memory the next call changes", "'ttyname_r'"},
    {"ptsname", "returns memory the next call changes", "'ptsname_r'"},
    {"swab", "reads and writes a length it cannot check", "a loop"},
    {"valloc", "returns memory that is not zeroed", "'calloc'"},
    {"memccpy", "returns a pointer past what it wrote", "a loop"},
    {"vfork", "shares the parent's memory", "a small function marked '__unsafe' that calls 'posix_spawn'"},
    {"brk", "takes memory away from under 'malloc'", "'malloc'"},
    {"sbrk", "takes memory away from under 'malloc'", "'malloc'"},
    {"freopen", "closes its stream even when it fails", "'fclose' and 'fopen'"},
    {"localeconv", "returns memory the next call changes",
     "a small function marked '__unsafe' that copies what you need"},
    {"setjmp", "goes back past frees and checks", "a result that says what failed"},
    {"longjmp", "goes back past frees and checks", "a result that says what failed"},
    {"atoi", "is undefined for a number too big", "'strtol'"},
    {"atol", "is undefined for a number too big", "'strtol'"},
    {"atoll", "is undefined for a number too big", "'strtoll'"},
};

bool clang::checkSafeCUnsafeFunction(Sema &S, const FunctionDecl *F,
                                     SourceLocation Loc, ArrayRef<Expr *> Args) {
  if (!S.getLangOpts().SafeC || !F || !F->getIdentifier() || !F->isExternC() ||
      S.getSourceManager().isInSystemHeader(Loc) ||
      !S.getSourceManager().isInSystemHeader(F->getLocation()))
    return false;
  for (const UnsafeFunction &U : UnsafeFunctions)
    if (F->getName() == U.Name) {
      // With no buffer it allocates one.
      if (U.Name == "realpath" && Args.size() == 2 &&
          Args[1]->isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNotNull))
        return false;
      S.SafeCDiag(Loc, "unsafe-function") << U.Name << U.Why << U.Instead;
      S.safeC().UnsafeCalls.push_back(Loc);
      return true;
    }
  return false;
}

StringRef clang::safeCPointerKind(QualType T) {
  return safec::pointerKind(T);
}

static bool hasKind(const ValueDecl *D, StringRef K) {
  return safeCPointerKind(D->getType()) == K;
}

static bool hasSafeCAnnotation(const Decl *D, StringRef Name) {
  for (const auto *A : D->specific_attrs<AnnotateAttr>())
    if (A->getAnnotation() == Name)
      return true;
  return false;
}

/// The annotation of \p D named \p Name, if any.
static const AnnotateAttr *getSafeCAnnotation(const Decl *D, StringRef Name) {
  for (const auto *A : D->specific_attrs<AnnotateAttr>())
    if (A->getAnnotation() == Name)
      return A;
  return nullptr;
}

/// A struct that ends in an array counted by one of its own fields.
static bool flexRecord(const RecordDecl *RD) {
  RD = RD ? RD->getDefinition() : nullptr;
  if (!RD || !RD->hasFlexibleArrayMember())
    return false;
  const FieldDecl *Last = nullptr;
  for (const FieldDecl *F : RD->fields())
    Last = F;
  return Last && (Last->getType()->getAs<CountAttributedType>() ||
                  flexRecord(Last->getType()->getAsRecordDecl()));
}
static bool flexRecord(QualType T) { return flexRecord(T->getAsRecordDecl()); }
static bool flexRecord(const DeclContext *DC) {
  return flexRecord(dyn_cast<RecordDecl>(DC));
}

static bool holdsPointer(QualType T) {
  T = T.getCanonicalType();
  if (const auto *AT = T->getAsArrayTypeUnsafe())
    return holdsPointer(AT->getElementType());
  if (const auto *At = T->getAs<AtomicType>())
    return holdsPointer(At->getValueType());
  if (const RecordDecl *RD = T->getAsRecordDecl())
    return llvm::any_of(RD->fields(), [](const FieldDecl *F) {
      return holdsPointer(F->getType());
    });
  return T->isAnyPointerType() || T->isBlockPointerType();
}

/// The tag field name of a tagged union, or "". A union that holds pointers is
/// not one: its pointer members are read and written only in '__unsafe'.
static StringRef taggedByName(const RecordDecl *U) {
  if (!U || !U->isUnion() ||
      holdsPointer(U->getASTContext().getCanonicalTagType(U)))
    return "";
  const AnnotateAttr *A = getSafeCAnnotation(U, "safec_tagged_by");
  if (!A || A->args_size() == 0)
    return "";
  if (const auto *SL =
          dyn_cast<StringLiteral>(A->args_begin()[0]->IgnoreParenImpCasts()))
    return SL->getString();
  return "";
}
static StringRef taggedByName(QualType T) {
  return taggedByName(T->getAsRecordDecl());
}

/// The tag values of each member of tagged union \p U, as written in _When.
static void whenValues(
    const ASTContext &Ctx, const RecordDecl *U,
    llvm::function_ref<void(const FieldDecl *, llvm::APSInt, SourceLocation)>
        Each) {
  for (const FieldDecl *M : U->fields())
    if (const AnnotateAttr *A = getSafeCAnnotation(M, "safec_when"))
      for (const Expr *V : llvm::make_range(A->args_begin(), A->args_end()))
        if (std::optional<llvm::APSInt> I = V->getIntegerConstantExpr(Ctx))
          Each(M, *I, V->getExprLoc());
}

/// M4: each tagged union member of \p RD names a sibling integer tag, and
/// each of its members says, with distinct values, when it is live.
static bool holdsOwner(QualType T) {
  if (T->isPointerType())
    return safeCPointerKind(T) == "owned";
  if (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    return holdsOwner(AT->getElementType());
  const RecordDecl *RD = T->getAsRecordDecl();
  RD = RD ? RD->getDefinition() : nullptr;
  return RD && llvm::any_of(RD->fields(), [](const FieldDecl *F) {
    return holdsOwner(F->getType());
  });
}

static void checkTaggedUnions(Sema &S, const RecordDecl *RD) {
  for (const FieldDecl *FD : RD->fields()) {
    if (const RecordDecl *U = FD->getType()->getAsRecordDecl();
        U && U->isUnion() && getSafeCAnnotation(U, "safec_tagged_by") &&
        holdsPointer(FD->getType()))
      S.SafeCDiag(FD->getLocation(), "tagged-pointers");
    StringRef Tag = taggedByName(FD->getType());
    if (Tag.empty())
      continue;
    const FieldDecl *TagField = nullptr;
    for (const FieldDecl *F : RD->fields())
      if (F->getName() == Tag)
        TagField = F;
    if (!TagField) {
      S.SafeCDiag(FD->getLocation(), "tag-field-missing") << Tag;
      continue;
    }
    if (!TagField->getType()->isIntegralOrEnumerationType())
      S.SafeCDiag(TagField->getLocation(), "tag-field-type") << Tag;
    const RecordDecl *U = FD->getType()->getAsRecordDecl();
    for (const FieldDecl *M : U->fields())
      if (!getSafeCAnnotation(M, "safec_when"))
        S.SafeCDiag(M->getLocation(), "when-missing") << M;
    llvm::SmallVector<std::pair<llvm::APSInt, const FieldDecl *>, 8> Seen;
    whenValues(S.Context, U,
               [&](const FieldDecl *M, llvm::APSInt V, SourceLocation L) {
                 for (auto &[W, Owner] : Seen)
                   if (llvm::APSInt::isSameValue(W, V) && Owner != M)
                     S.SafeCDiag(L, "when-duplicate")
                         << toString(V, 10) << Owner << M;
                 Seen.push_back({V, M});
               });
  }
}

/// A call through a pointer knows only the pointer's type, so a function may
/// be stored only in one that makes the same promises.
void clang::checkSafeCFunctionConversion(Sema &S, QualType Dst,
                                         const Expr *Src) {
  QualType ST = Src->getType();
  if (ST->isFunctionPointerType())
    ST = ST->getPointeeType();
  if (!Dst->isFunctionPointerType())
    return;
  const auto *DP = Dst->getPointeeType()->getAs<FunctionProtoType>();
  const auto *SP = ST->getAs<FunctionProtoType>();
  if (!DP || !SP || DP->getNumParams() != SP->getNumParams() ||
      S.getSourceManager().isInSystemHeader(Src->getExprLoc()))
    return;
  // What ends a string is part of what the callee does with it.
  auto Ends = [&](QualType T) {
    const auto *VT = T->getAs<ValueTerminatedType>();
    return VT ? toString(VT->getTerminatorValue(S.Context), 10) : std::string();
  };
  if (Ends(DP->getReturnType()) != Ends(SP->getReturnType())) {
    S.SafeCDiag(Src->getExprLoc(), "fn-promise") << "the result" << Dst;
    return;
  }
  for (unsigned I = 0, N = DP->getNumParams(); I != N; ++I)
    if (safec::promise(DP->getParamType(I)) != safec::promise(SP->getParamType(I)) ||
        Ends(DP->getParamType(I)) != Ends(SP->getParamType(I))) {
      S.SafeCDiag(Src->getExprLoc(), "fn-promise")
          << ("parameter " + std::to_string(I + 1)) << Dst;
      return;
    }
  if (safec::promise(DP->getReturnType()) != safec::promise(SP->getReturnType()))
    S.SafeCDiag(Src->getExprLoc(), "fn-promise") << "the result" << Dst;
}

void clang::checkSafeCPointerStorage(Sema &S, Decl *D) {
  auto *RD = dyn_cast<RecordDecl>(D);
  if (!RD || S.getSourceManager().isInSystemHeader(D->getLocation()))
    return;
  for (const FieldDecl *FD : RD->fields())
    if (flexRecord(S.Context.getBaseElementType(FD->getType())))
      S.SafeCDiag(FD->getLocation(), "flex-nested")
          << S.Context.getBaseElementType(FD->getType());
  checkTaggedUnions(S, RD);
  if (RD->isUnion() && std::distance(RD->field_begin(), RD->field_end()) > 1)
    for (const FieldDecl *FD : RD->fields())
      if (getSafeCAnnotation(FD, "safec_when") == nullptr && holdsOwner(FD->getType()))
        S.SafeCDiag(FD->getLocation(), "owned-in-union") << FD;
}

/// The field or variable an lvalue names directly (x, s.f, p->f), if any.
static const ValueDecl *namedStorage(const Expr *E) {
  E = safec::peel(E);
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl();
  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return ME->getMemberDecl();
  return nullptr;
}

/// A member of a tagged union that holds pointers in some member, named by
/// \p E (a member access), or null. Its address must not be taken: a later
/// tag change would let the pointer read another member's bytes.
namespace {
static bool isTagField(const FieldDecl *F);
}
static const FieldDecl *pointerUnionMember(const ASTContext &Ctx,
                                           const Expr *E, StringRef &Tag) {
  const auto *ME = dyn_cast<MemberExpr>(E->IgnoreParens());
  const auto *F = ME ? dyn_cast<FieldDecl>(ME->getMemberDecl()) : nullptr;
  const RecordDecl *U = F ? F->getParent() : nullptr;
  if (!U)
    return nullptr;
  // `t.u.m.x`, `t.u.m[i].x`: part of member m.
  if (!U->isUnion() || taggedByName(U).empty()) {
    if (ME->isArrow())
      return nullptr;
    const Expr *B = ME->getBase()->IgnoreParenImpCasts();
    while (const auto *ASE = dyn_cast<ArraySubscriptExpr>(B))
      B = ASE->getBase()->IgnoreParenImpCasts();
    return pointerUnionMember(Ctx, B, Tag);
  }
  Tag = taggedByName(U);
  if (Tag.empty())
    return nullptr;
  for (const FieldDecl *M : U->fields())
    if (!Sema::isSafeCPlainData(const_cast<ASTContext &>(Ctx), M->getType(),
                                /*VoidIsPlain=*/true))
      return F;
  return nullptr;
}

namespace {
class PointerKindChecker : public RecursiveASTVisitor<PointerKindChecker> {
  Sema &S;
  /// `char *t = strchr(constant, 'x')`.
  void checkConstSearch(const Expr *Src) {
    const auto *Call =
        dyn_cast<CallExpr>(safec::peel(Src)->IgnoreParenCasts());
    const FunctionDecl *F = Call ? Call->getDirectCallee() : nullptr;
    // Text the library keeps.
    if (F && F->isExternC() && F->getIdentifier() &&
        llvm::is_contained({"strerror", "getenv", "setlocale", "strsignal", "tmpnam"},
                           F->getName())) {
      S.SafeCDiag(Call->getExprLoc(), "const-search") << F;
      return;
    }
    if (!F || !F->isExternC() || !F->getIdentifier() ||
        !llvm::is_contained({"strchr", "strrchr", "strstr", "strpbrk", "memchr",
                             "strcasestr", "strnstr", "index", "rindex"},
                            F->getName()))
      return;
    const Expr *A = Call->getArg(0);
    for (;;) {
      A = safec::peel(A)->IgnoreParens();
      if (const auto *PPE = dyn_cast<BoundsSafetyPointerPromotionExpr>(A))
        A = PPE->getPointer();
      else if (const auto *T = dyn_cast<TerminatedByFromIndexableExpr>(A))
        A = T->getPointer();
      else
        break;
    }
    QualType T = A->getType();
    QualType Elem = T->isPointerType() ? T->getPointeeType()
                    : T->isArrayType()
                        ? QualType(T->getPointeeOrArrayElementType(), 0)
                        : QualType();
    bool Const = !Elem.isNull() && (Elem.isConstQualified() ||
                                    T.isConstQualified() ||
                                    isa<StringLiteral>(A));
    if (const auto *AT = T->getAsArrayTypeUnsafe())
      Const |= AT->getElementType().isConstQualified();
    if (Const)
      S.SafeCDiag(Call->getExprLoc(), "const-search") << F;
  }

  void checkStore(QualType Dst, const Expr *Src) {
    if (Dst->isPointerType() && !Dst->getPointeeType().isConstQualified())
      checkConstSearch(Src);
  }

public:
  // The function body being checked, if any (null for global initializers).
  const Stmt *Body = nullptr;
  explicit PointerKindChecker(Sema &S, const Stmt *Body = nullptr)
      : S(S), Body(Body) {}

  /// A call that may free or change memory: one given a pointer it may write
  /// through, or an owner. (A global holds no pointer that can end.)
  static bool mayChange(const CallExpr *C) {
    const FunctionDecl *F = C->getDirectCallee();
    if (unsigned ID = C->getBuiltinCallee();
        ID == Builtin::BI__builtin_object_size ||
        ID == Builtin::BI__builtin_dynamic_object_size)
      return false; // evaluates nothing
    QualType CT = C->getCallee()->getType();
    const auto *FT = F ? F->getType()->getAs<FunctionProtoType>()
                     : CT->isFunctionPointerType()
                           ? CT->getPointeeType()->getAs<FunctionProtoType>()
                           : nullptr;
    if (!FT)
      return true;
    for (unsigned I = 0; I < C->getNumArgs(); ++I) {
      QualType T = I < FT->getNumParams() ? FT->getParamType(I)
                                          : C->getArg(I)->getType();
      if (T->isRecordType() ||
          (T->isAnyPointerType() && !T->getPointeeType().isConstQualified() &&
           !T->isFunctionPointerType()))
        return true;
    }
    return false;
  }
  struct Effects : RecursiveASTVisitor<Effects> {
    bool Call = false, PointerSet = false;
    bool VisitCallExpr(CallExpr *C) {
      Call |= mayChange(C);
      return true;
    }
    bool VisitBinaryOperator(BinaryOperator *B) {
      if (B->isAssignmentOp())
        PointerSet |= !B->getLHS()->getType()->isArithmeticType();
      return true;
    }
    bool VisitUnaryOperator(UnaryOperator *U) {
      if (U->isIncrementDecrementOp())
        PointerSet |= U->getType()->isAnyPointerType();
      return true;
    }
    bool VisitOpaqueValueExpr(OpaqueValueExpr *O) {
      return !O->getSourceExpr() || TraverseStmt(O->getSourceExpr());
    }
  };
  struct PointerReads : RecursiveASTVisitor<PointerReads> {
    bool Memory = false, Variable = false;
    bool VisitImplicitCastExpr(ImplicitCastExpr *C) {
      if (C->getCastKind() == CK_LValueToRValue && C->getType()->isAnyPointerType()) {
        const Expr *L = C->getSubExpr()->IgnoreParens();
        const auto *D = dyn_cast<DeclRefExpr>(L);
        const auto *V = D ? dyn_cast<VarDecl>(D->getDecl()) : nullptr;
        (V && V->hasLocalStorage() ? Variable : Memory) = true;
      }
      return true;
    }
    bool VisitOpaqueValueExpr(OpaqueValueExpr *O) {
      return !O->getSourceExpr() || TraverseStmt(O->getSourceExpr());
    }
  };
  /// A reads pointers; B must not change them.
  void checkOrder(const Expr *A, const Expr *B, SourceLocation L) {
    Effects E;
    E.TraverseStmt(const_cast<Expr *>(B));
    if (!E.Call && !E.PointerSet)
      return;
    PointerReads P;
    P.TraverseStmt(const_cast<Expr *>(A));
    if ((P.Memory && (E.Call || E.PointerSet)) || (P.Variable && E.PointerSet))
      S.SafeCDiag(L, "order");
  }

  /// The counted pointer or count an assignment sets, and the variable its
  /// struct is reached from.
  struct GroupPart {
    const ValueDecl *Set = nullptr;
    const VarDecl *Base = nullptr;
  };
  static bool countLinked(const ValueDecl *A, const ValueDecl *B) {
    for (int I = 0; I < 2; ++I, std::swap(A, B))
      if (const auto *CAT = A->getType()->getAs<CountAttributedType>())
        for (const TypeCoupledDeclRefInfo &D : CAT->getCoupledDecls())
          if (D.getDecl() == B)
            return true;
    return false;
  }
  static const Expr *assigned(const Stmt *St) {
    const auto *E = dyn_cast<Expr>(St);
    E = E ? E->IgnoreParenImpCasts() : nullptr;
    if (const auto *B = dyn_cast_or_null<BinaryOperator>(E))
      return B->isAssignmentOp() ? B->getLHS()->IgnoreParens() : nullptr;
    if (const auto *U = dyn_cast_or_null<UnaryOperator>(E))
      return U->isIncrementDecrementOp() ? U->getSubExpr()->IgnoreParens() : nullptr;
    return nullptr;
  }
  /// The statement as written, inside what bounds safety wraps a group in.
  static const Stmt *written(const Stmt *St) {
    while (true) {
      if (const auto *EC = dyn_cast<ExprWithCleanups>(St))
        St = EC->getSubExpr();
      else if (const auto *M = dyn_cast<MaterializeSequenceExpr>(St))
        St = M->getWrappedExpr();
      else if (const auto *B = dyn_cast<BoundsCheckExpr>(St))
        St = B->getGuardedExpr();
      else if (const auto *P = dyn_cast<ParenExpr>(St))
        St = P->getSubExpr();
      else
        return St;
    }
  }
  static GroupPart groupPart(const Stmt *St) {
    GroupPart G;
    const Expr *L = assigned(written(St));
    const auto *ME = dyn_cast_or_null<MemberExpr>(L);
    const auto *DRE = dyn_cast_or_null<DeclRefExpr>(L);
    const ValueDecl *D = ME ? ME->getMemberDecl() : DRE ? DRE->getDecl() : nullptr;
    if (!D || !(D->hasAttr<DependerDeclsAttr>() ||
                D->getType()->getAs<CountAttributedType>()))
      return G;
    G.Set = D;
    const Expr *B = L;
    while (const auto *M = dyn_cast<MemberExpr>(B))
      B = M->getBase()->IgnoreParenImpCasts();
    if (const auto *R = dyn_cast<DeclRefExpr>(B))
      G.Base = dyn_cast<VarDecl>(R->getDecl());
    return G;
  }
  static void flatten(const Stmt *St, SmallVectorImpl<const Stmt *> &Out) {
    const auto *E = dyn_cast_or_null<Expr>(written(St));
    if (const auto *B = E ? dyn_cast<BinaryOperator>(E->IgnoreParens()) : nullptr;
        B && B->getOpcode() == BO_Comma) {
      flatten(B->getLHS(), Out);
      flatten(B->getRHS(), Out);
    } else {
      Out.push_back(St);
    }
  }
  bool VisitCompoundStmt(CompoundStmt *CS) {
    SmallVector<const Stmt *, 8> Body;
    for (const Stmt *St : CS->body())
      flatten(St, Body);
    checkGroups(Body);
    return true;
  }
  llvm::DenseSet<SourceLocation> Between;
  void checkGroups(ArrayRef<const Stmt *> Body) {
    for (size_t I = 0; I < Body.size(); ++I) {
      GroupPart A = groupPart(Body[I]);
      if (!A.Set)
        continue;
      for (size_t J = I + 1; J < Body.size() && assigned(written(Body[J])); ++J) {
        GroupPart B = groupPart(Body[J]);
        if (!B.Set || !countLinked(A.Set, B.Set))
          continue;
        for (size_t K = I + 1; K < J; ++K) {
          if (groupPart(Body[K]).Set)
            continue;
          const Expr *L = assigned(written(Body[K]));
          if (Between.insert(L->getExprLoc()).second)
            S.SafeCDiag(L->getExprLoc(), "count-group-between");
        }
      }
    }
  }

  /// `s->h = z`, `&s->h`, where a count `__counted_by(h.n)` is inside h.
  void checkNestedCount(const Expr *E, bool Address = false) {
    const auto *ME = dyn_cast<MemberExpr>(E->IgnoreParens());
    if (Address && ME && ME->getType()->isIntegralOrEnumerationType() &&
        ME->getMemberDecl()->hasAttr<DependerDeclsAttr>())
      S.SafeCDiag(E->getExprLoc(), "count-address") << ME->getMemberDecl();
    // `s->h.n = k`, where the count is h.n.
    for (const auto *B = ME ? dyn_cast<MemberExpr>(ME->getBase()->IgnoreParens())
                            : nullptr;
         B; B = dyn_cast<MemberExpr>(B->getBase()->IgnoreParens()))
      if (B->getType()->isRecordType() &&
          B->getMemberDecl()->hasAttr<DependerDeclsAttr>() &&
          flexRecord(B->getMemberDecl()->getDeclContext()))
        S.SafeCDiag(E->getExprLoc(), "nested-count") << B->getMemberDecl();
    if (ME && ME->getType()->isRecordType() &&
        ME->getMemberDecl()->hasAttr<DependerDeclsAttr>())
      S.SafeCDiag(E->getExprLoc(), "nested-count") << ME->getMemberDecl();
  }
  bool VisitBinaryOperator(BinaryOperator *BO) {
    // (Not the sums bounds safety makes of values it already has.)
    if (BO->isAdditiveOp() && BO->getType()->isAnyPointerType() &&
        !isa<OpaqueValueExpr>(BO->getLHS()->IgnoreParenCasts()) &&
        !isa<OpaqueValueExpr>(BO->getRHS()->IgnoreParenCasts())) {
      checkOrder(BO->getLHS(), BO->getRHS(), BO->getOperatorLoc());
      checkOrder(BO->getRHS(), BO->getLHS(), BO->getOperatorLoc());
    }
    if (BO->isAssignmentOp() &&
        BO->getLHS()->getType()->isPointerTypeWithBounds())
      checkOrder(BO->getLHS(), BO->getRHS(), BO->getOperatorLoc());
    if (BO->isAssignmentOp()) {
      const auto *ME = dyn_cast<MemberExpr>(BO->getLHS()->IgnoreParens());
      if (ME && (ME->getMemberDecl()->hasAttr<DependerDeclsAttr>() ||
                 ME->getType()->getAs<CountAttributedType>()) &&
          !groupPart(BO).Base)
        S.SafeCDiag(BO->getOperatorLoc(), "count-group-base");
    }
    if (BO->getOpcode() == BO_Comma) {
      SmallVector<const Stmt *, 4> Body;
      flatten(BO, Body);
      checkGroups(Body);
    }
    if (BO->isAssignmentOp())
      checkNestedCount(BO->getLHS());
    if (BO->isCompoundAssignmentOp())
      if (const auto *ME = dyn_cast<MemberExpr>(BO->getLHS()->IgnoreParens()))
        if (const auto *F = dyn_cast<FieldDecl>(ME->getMemberDecl());
            F && isTagField(F))
          S.SafeCDiag(BO->getOperatorLoc(), "tag-address") << F;
    if (BO->getOpcode() == BO_Assign)
      checkStore(BO->getLHS()->getType(), BO->getRHS());
    return true;
  }

  /// v*printf takes its arguments on trust: allowed only to forward a checked
  /// function's own, once.
  void checkVaList(const CallExpr *Call, const FunctionDecl *F) {
    const auto *FA = F->getAttr<FormatAttr>();
    if (!FA || FA->getFirstArg() != 0 || !F->getIdentifier() ||
        !S.getSourceManager().isInSystemHeader(F->getLocation()))
      return;
    bool Takes = false;
    for (const ParmVarDecl *P : F->parameters())
      Takes |= S.Context.hasSameUnqualifiedType(
                   P->getType(), S.Context.getBuiltinVaListType()) ||
               P->getType().getAsString().find("va_list") != std::string::npos;
    if (!Takes)
      return;
    const FunctionDecl *Cur = S.getCurFunctionDecl();
    const auto *Own = Cur ? Cur->getAttr<FormatAttr>() : nullptr;
    const auto *DRE =
        FA->getFormatIdx() <= int(Call->getNumArgs())
            ? dyn_cast<DeclRefExpr>(safec::peel(
                  Call->getArg(FA->getFormatIdx() - 1)))
            : nullptr;
    bool Ok = Own && Own->getFirstArg() > 0 && DRE &&
              isa<ParmVarDecl>(DRE->getDecl()) &&
              int(cast<ParmVarDecl>(DRE->getDecl())->getFunctionScopeIndex()) ==
                  Own->getFormatIdx() - 1;
    struct Scan : RecursiveASTVisitor<Scan> {
      Sema *S;
      SourceLocation At;
      unsigned Uses = 0;
      const ValueDecl *Fmt = nullptr;
      bool Loop = false, Ended = false, Changed = false;
      bool names(const Expr *E) {
        const auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
        return Fmt && DRE && DRE->getDecl() == Fmt;
      }
      bool VisitUnaryOperator(UnaryOperator *U) {
        Changed |=
            (U->isIncrementDecrementOp() || U->getOpcode() == UO_AddrOf) &&
            names(U->getSubExpr());
        return true;
      }
      bool VisitBinaryOperator(BinaryOperator *B) {
        Changed |= B->isAssignmentOp() && names(B->getLHS());
        return true;
      }
      bool VisitCallExpr(CallExpr *C) {
        const FunctionDecl *FD = C->getDirectCallee();
        if (FD && FD->getIdentifier() &&
            (FD->getName().contains("va_end") ||
             FD->getName().contains("va_copy")))
          Ended |= S->getSourceManager().isBeforeInTranslationUnit(
              C->getExprLoc(), At);
        if (const auto *A = FD ? FD->getAttr<FormatAttr>() : nullptr)
          Uses += A->getFirstArg() == 0;
        return true;
      }
      bool VisitForStmt(ForStmt *) { return Loop = true; }
      bool VisitWhileStmt(WhileStmt *) { return Loop = true; }
      bool VisitDoStmt(DoStmt *) { return Loop = true; }
      bool VisitGotoStmt(GotoStmt *) { return Loop = true; }
    } Sc;
    Sc.S = &S;
    Sc.At = Call->getExprLoc();
    // The callers' arguments were checked against the format as passed.
    Sc.Fmt = DRE ? DRE->getDecl() : nullptr;
    Sc.TraverseStmt(const_cast<Stmt *>(Body));
    if (!Ok || Sc.Changed || Sc.Loop || Sc.Ended || Sc.Uses != 1)
      S.SafeCDiag(Call->getExprLoc(), "valist");
  }
  bool checkParam(const ParmVarDecl *P) {
    QualType T = P->getType();
    while (T->isPointerType()) {
      if (T.isRestrictQualified())
        S.SafeCDiag(P->getLocation(), "restrict");
      T = T->getPointeeType();
    }
    if (T->isVariablyModifiedType())
      S.SafeCDiag(P->getLocation(), "vla-param");
    return true;
  }
  bool VisitMatrixOrVector(const Expr *Base, const Expr *Idx) {
    if (Base->IgnoreParenImpCasts()->getType()->isVectorType() &&
        [&] {
          auto V = Idx->getIntegerConstantExpr(S.Context);
          const auto *VT = Base->IgnoreParenImpCasts()
                               ->getType()
                               ->getAs<VectorType>();
          return !V || !VT || V->isNegative() || V->uge(VT->getNumElements());
        }())
      S.SafeCDiag(Idx->getExprLoc(), "vector-index");
    return true;
  }
  /// The language Safe C has rules for. Anything else is an error: a kind of
  /// statement, expression or cast is added here only with the rules that
  /// cover it (safe-c-docs/rules.md lists them).
  bool VisitStmt(Stmt *St) {
    markTested(St);
    switch (St->getStmtClass()) {
    case Stmt::NullStmtClass:
    case Stmt::CompoundStmtClass:
    case Stmt::DeclStmtClass:
    case Stmt::IfStmtClass:
    case Stmt::SwitchStmtClass:
    case Stmt::WhileStmtClass:
    case Stmt::DoStmtClass:
    case Stmt::ForStmtClass:
    case Stmt::GotoStmtClass:
    case Stmt::ReturnStmtClass:
    case Stmt::CaseStmtClass:
    case Stmt::DefaultStmtClass:
    case Stmt::ContinueStmtClass:
    case Stmt::BreakStmtClass:
    case Stmt::LabelStmtClass:
    case Stmt::AttributedStmtClass:
    case Stmt::PredefinedExprClass:
    case Stmt::DeclRefExprClass:
    case Stmt::IntegerLiteralClass:
    case Stmt::FloatingLiteralClass:
    case Stmt::ImaginaryLiteralClass:
    case Stmt::StringLiteralClass:
    case Stmt::CharacterLiteralClass:
    case Stmt::ParenExprClass:
    case Stmt::UnaryOperatorClass:
    case Stmt::OffsetOfExprClass:
    case Stmt::UnaryExprOrTypeTraitExprClass:
    case Stmt::ArraySubscriptExprClass:
    case Stmt::CallExprClass:
    case Stmt::MemberExprClass:
    case Stmt::BinaryOperatorClass:
    case Stmt::CompoundAssignOperatorClass:
    case Stmt::ConditionalOperatorClass:
    case Stmt::BinaryConditionalOperatorClass:
    case Stmt::ImplicitCastExprClass:
    case Stmt::CStyleCastExprClass:
    case Stmt::CompoundLiteralExprClass:
    case Stmt::InitListExprClass:
    case Stmt::DesignatedInitExprClass:
    case Stmt::DesignatedInitUpdateExprClass:
    case Stmt::ImplicitValueInitExprClass:
    case Stmt::NoInitExprClass:
    case Stmt::ParenListExprClass:
    case Stmt::GenericSelectionExprClass:
    case Stmt::SourceLocExprClass:
    case Stmt::ConstantExprClass:
    case Stmt::AtomicExprClass:
    case Stmt::StmtExprClass:
    case Stmt::ChooseExprClass:
    case Stmt::GNUNullExprClass:
    case Stmt::CXXNullPtrLiteralExprClass:
    case Stmt::CXXBoolLiteralExprClass:
    case Stmt::TypeTraitExprClass:
    case Stmt::ExprWithCleanupsClass:
    case Stmt::OpaqueValueExprClass:
    case Stmt::RecoveryExprClass: // already an error
    case Stmt::EmbedExprClass:
    case Stmt::ShuffleVectorExprClass:
    case Stmt::MaterializeTemporaryExprClass: // an array in a returned struct
    case Stmt::ConvertVectorExprClass:
    // Have their own rules, which reject or check them.
    case Stmt::GCCAsmStmtClass:
    case Stmt::VAArgExprClass:
    case Stmt::BlockExprClass:
    // Made by -fbounds-safety.
    case Stmt::AssumptionExprClass:
    case Stmt::BoundsSafetyPointerPromotionExprClass:
    case Stmt::ForgePtrExprClass:
    case Stmt::GetBoundExprClass:
    case Stmt::PredefinedBoundsCheckExprClass:
    case Stmt::BoundsCheckExprClass:
    case Stmt::MaterializeSequenceExprClass:
    case Stmt::TerminatedByToIndexableExprClass:
    case Stmt::TerminatedByFromIndexableExprClass:
      break;
    default:
      S.SafeCDiag(St->getBeginLoc(), "not-in-safe-c") << St->getStmtClassName();
      break;
    }
    if (const auto *D = dyn_cast<DesignatedInitExpr>(St))
      for (const DesignatedInitExpr::Designator &Des : D->designators())
        // Plain data and no side effects: copying it is what the programmer
        // means. (Not isEvaluatable: it asserts on a syntactic init list.)
        if (Des.isArrayRangeDesignator() &&
            (D->getInit()->HasSideEffects(S.Context) ||
             !S.isSafeCPlainData(S.Context, D->getInit()->getType(), true))) {
          S.SafeCDiag(D->getBeginLoc(), "range-designator");
          break;
        }
    if (const auto *C = dyn_cast<CastExpr>(St))
      switch (C->getCastKind()) {
      case CK_BitCast:
      case CK_LValueToRValue:
      case CK_NoOp:
      case CK_ArrayToPointerDecay:
      case CK_FunctionToPointerDecay:
      case CK_NullToPointer:
      case CK_IntegralToPointer: // rejected with its own message
      case CK_PointerToIntegral:
      case CK_PointerToBoolean:
      case CK_ToVoid:
      case CK_VectorSplat:
      case CK_IntegralCast:
      case CK_IntegralToBoolean:
      case CK_IntegralToFloating:
      case CK_FloatingToIntegral:
      case CK_FloatingToBoolean:
      case CK_BooleanToSignedIntegral:
      case CK_FloatingCast:
      case CK_FloatingRealToComplex:
      case CK_FloatingComplexToReal:
      case CK_FloatingComplexToBoolean:
      case CK_FloatingComplexCast:
      case CK_FloatingComplexToIntegralComplex:
      case CK_IntegralRealToComplex:
      case CK_IntegralComplexToReal:
      case CK_IntegralComplexToBoolean:
      case CK_IntegralComplexCast:
      case CK_IntegralComplexToFloatingComplex:
      case CK_AtomicToNonAtomic:
      case CK_NonAtomicToAtomic:
      case CK_BuiltinFnToFnPtr:
      case CK_BoundsSafetyPointerCast:
      case CK_Dependent:
        break;
      default:
        S.SafeCDiag(St->getBeginLoc(), "not-in-safe-c") << C->getCastKindName();
        break;
      }
    return true;
  }
  /// `atomic_store(a + i, v)`: the operation takes a plain address.
  void checkAtomicPointer(const Expr *P) {
    if (!P->getType()->isPointerType())
      return;
    P = safec::peel(P)->IgnoreParenCasts();
    if (const auto *UO = dyn_cast<UnaryOperator>(P);
        UO && UO->getOpcode() == UO_AddrOf) {
      const Expr *L = UO->getSubExpr()->IgnoreParens();
      const auto *DRE = dyn_cast<DeclRefExpr>(L);
      if (DRE && DRE->getDecl()->hasAttr<DependerDeclsAttr>())
        S.SafeCDiag(P->getExprLoc(), "count-address") << DRE->getDecl();
      if (DRE || isa<MemberExpr>(L))
        return;
      S.SafeCDiag(P->getExprLoc(), "atomic-pointer");
      return;
    }
    if (P->getType()->isArrayType() || P->getType()->isPointerTypeWithBounds() ||
        P->getType()->isBoundsAttributedType())
      S.SafeCDiag(P->getExprLoc(), "atomic-pointer");
  }
  bool VisitAtomicExpr(AtomicExpr *E) {
    checkAtomicPointer(E->getPtr());
    if (E->isCmpXChg())
      checkAtomicPointer(E->getVal1());
    if (E->getOpAsString().starts_with("__atomic_") ||
        E->getOpAsString().starts_with("__scoped_"))
      S.SafeCDiag(E->getExprLoc(), "builtin-unchecked-name")
          << E->getOpAsString();
    return true;
  }
  bool VisitMemberExprFields(MemberExpr *E) {
    const auto *F = dyn_cast<FieldDecl>(E->getMemberDecl());
    if (F && isLibraryHandle(S.Context.getCanonicalTagType(F->getParent())) &&
        !S.getSourceManager().isInSystemMacro(E->getExprLoc()))
      S.SafeCDiag(E->getExprLoc(), "library-field") << F->getParent();
    return true;
  }
  bool VisitBlockExpr(BlockExpr *E) {
    S.SafeCDiag(E->getExprLoc(), "block");
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr *E) {
    collectVoid();
    const auto *F = dyn_cast<FunctionDecl>(E->getDecl());
    if (F && llvm::is_contained(S.safeC().UnsafeCalls, E->getExprLoc()))
      return true;
    // Lock requirements are not part of a pointer's type.
    if (F && !DirectCallees.contains(E) &&
        (F->hasAttr<RequiresCapabilityAttr>() ||
         F->hasAttr<AcquireCapabilityAttr>() ||
         F->hasAttr<ReleaseCapabilityAttr>() ||
         F->hasAttr<TryAcquireCapabilityAttr>()))
      S.SafeCDiag(E->getExprLoc(), "fn-library-pointer") << F;
    if (!F || DirectCallees.contains(E) || !F->getIdentifier() ||
        !S.getSourceManager().isInSystemHeader(F->getLocation()))
      return true;
    // Harmless: no pointer is written through, and no bounds are needed.
    if (llvm::is_contained({"malloc", "calloc", "free", "realloc"},
                           F->getName()))
      return true;
    bool Plain = !F->isVariadic() && !F->hasAttr<ReturnsTwiceAttr>() &&
                 F->getName() != "longjmp" && F->getName() != "_longjmp" &&
                 F->getName() != "siglongjmp";
    for (const ParmVarDecl *P : F->parameters())
      Plain &= !P->getType()->isAnyPointerType();
    if (!Plain)
      S.SafeCDiag(E->getExprLoc(), "fn-library-pointer") << F;
    return true;
  }
  llvm::SmallPtrSet<const Expr *, 16> DirectCallees;
  // `(c ? del : keep)(&s)`: the common type drops what they promise.
  bool VisitConditionalOperator(ConditionalOperator *E) {
    if (E->getType()->isFunctionPointerType()) {
      checkSafeCFunctionConversion(S, E->getType(),
                                   E->getTrueExpr()->IgnoreParenImpCasts());
      checkSafeCFunctionConversion(S, E->getType(),
                                   E->getFalseExpr()->IgnoreParenImpCasts());
    }
    return true;
  }
  bool VisitMemberExpr(MemberExpr *E) {
    VisitMemberExprFields(E);
    if (!E->isArrow() &&
        E->getBase()->IgnoreParenImpCasts()->getType()->isAtomicType())
      S.SafeCDiag(E->getExprLoc(), "atomic-member");
    return true;
  }
  bool VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
    checkOrder(E->getBase(), E->getIdx(), E->getExprLoc());
    checkOrder(E->getIdx(), E->getBase(), E->getExprLoc());
    VisitMatrixOrVector(E->getBase(), E->getIdx());
    return true;
  }
  /// Conversions seen inside a direct call's argument, where the byte-write
  /// check covers them, or compared.
  llvm::SmallPtrSet<const Expr *, 16> VoidOk;
  void allowVoid(const Expr *E) {
    while (E) {
      VoidOk.insert(E);
      if (const auto *C = dyn_cast<CastExpr>(E))
        E = C->getSubExpr();
      else if (const auto *P = dyn_cast<ParenExpr>(E))
        E = P->getSubExpr();
      else if (const auto *O = dyn_cast<OpaqueValueExpr>(E))
        E = O->getSourceExpr();
      else if (const auto *M = dyn_cast<MaterializeSequenceExpr>(E))
        E = M->getWrappedExpr();
      else if (const auto *B = dyn_cast<BoundsSafetyPointerPromotionExpr>(E))
        E = B->getPointer();
      else if (const auto *BC = dyn_cast<BoundsCheckExpr>(E))
        E = BC->getGuardedExpr();
      else
        break;
    }
  }
  // Before anything else: bounds-safety wrappers can put an argument ahead of
  // its call in the visit order.
  bool CollectedVoid = false;
  void collectVoid() {
    if (CollectedVoid)
      return;
    CollectedVoid = true;
    struct Finder : RecursiveASTVisitor<Finder> {
      PointerKindChecker *C;
      bool VisitCallExpr(CallExpr *Call) {
        const FunctionDecl *F = Call->getDirectCallee();
        if (F)
          C->DirectCallees.insert(Call->getCallee()->IgnoreParenImpCasts());
        for (unsigned I = 0, N = Call->getNumArgs(); I != N; ++I) {
          // Through a pointer, only an argument that is freed.
          QualType T = Call->getCallee()->getType();
          const auto *FT = T->isFunctionPointerType()
                               ? T->getPointeeType()->getAs<FunctionProtoType>()
                               : nullptr;
          bool Takes = FT && I < FT->getNumParams() &&
                       safeCPointerKind(FT->getParamType(I)) == "owned";
          if (F || Takes)
            C->allowVoid(Call->getArg(I));
        }
        return true;
      }
      bool VisitBinaryOperator(BinaryOperator *BO) {
        if (BO->isComparisonOp()) {
          C->allowVoid(BO->getLHS());
          C->allowVoid(BO->getRHS());
        }
        return true;
      }
    } F;
    F.C = this;
    F.TraverseStmt(const_cast<Stmt *>(Body));
  }
  bool VisitGuardedAddress(UnaryOperator *UO) {
    if (UO->getOpcode() != UO_AddrOf)
      return true;
    collectVoid();
    const Expr *E = UO->getSubExpr()->IgnoreParens();
    const ValueDecl *D = nullptr;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
      D = DRE->getDecl();
    else if (const auto *ME = dyn_cast<MemberExpr>(E))
      D = ME->getMemberDecl();
    if (D && D->hasAttr<GuardedByAttr>() && !VoidOk.contains(UO))
      S.SafeCDiag(UO->getOperatorLoc(), "guarded-address") << D->getName();
    return true;
  }
  bool isLibraryHandle(QualType T) {
    const RecordDecl *RD = T->getAsRecordDecl();
    return RD && S.getSourceManager().isInSystemHeader(RD->getLocation()) &&
           llvm::is_contained({"__sFILE", "_IO_FILE", "_dirdesc", "__dirstream",
                               "DIR"},
                              RD->getName());
  }
  // Arguments of 'free', which writes nothing.
  llvm::SmallPtrSet<const Expr *, 8> FlexMayConvert;
  bool VisitCastExpr(CastExpr *C) {
    QualType FromT = C->getSubExpr()->getType(), ToT = C->getType();
    if (FromT->isPointerType() && ToT->isPointerType()) {
      const bool Same = S.Context.hasSameUnqualifiedType(
          FromT->getPointeeType(), ToT->getPointeeType());
      if (!Same && flexRecord(FromT->getPointeeType()) &&
          !FlexMayConvert.count(C) && !ToT->getPointeeType().isConstQualified())
        S.SafeCDiag(C->getExprLoc(), "flex-bytes") << FromT->getPointeeType();
      if (const RecordDecl *To = ToT->getPointeeType()->getAsRecordDecl();
          !Same && To && !To->getDefinition() && !FromT->isVoidPointerType() &&
          C->getCastKind() != CK_NullToPointer && isa<ExplicitCastExpr>(C))
        S.SafeCDiag(C->getExprLoc(), "cast-incomplete") << ToT->getPointeeType();
      if (const RecordDecl *From = FromT->getPointeeType()->getAsRecordDecl();
          !Same && From && !From->getDefinition() && !ToT->isVoidPointerType() &&
          isa<ExplicitCastExpr>(C))
        S.SafeCDiag(C->getExprLoc(), "cast-incomplete")
            << FromT->getPointeeType();
      if (!Same && flexRecord(ToT->getPointeeType()) &&
          !FromT->isVoidPointerType() &&
          C->getCastKind() != CK_NullToPointer)
        S.SafeCDiag(C->getExprLoc(), "flex-cast") << ToT->getPointeeType();
      if (ToT->getPointeeType()->isVariablyModifiedType() &&
          !FromT->getPointeeType()->isVariablyModifiedType() &&
          C->getCastKind() != CK_ArrayToPointerDecay)
        S.SafeCDiag(C->getExprLoc(), "vla-pointer") << ToT;
      if (ToT->isFunctionPointerType() && FromT->isFunctionPointerType())
        if (ToT->getPointeeType()->castAs<FunctionType>()->getNoReturnAttr() &&
            !FromT->getPointeeType()->castAs<FunctionType>()->getNoReturnAttr())
          S.SafeCDiag(C->getExprLoc(), "fn-promise") << "'noreturn'" << ToT;
    }
    if (QualType From = C->getSubExpr()->getType();
        From->isPointerType() &&
        From->getPointeeType()->isAtomicType() &&
        C->getType()->isPointerType() &&
        !C->getType()->getPointeeType()->isAtomicType() &&
        !C->getType()->isVoidPointerType())
      S.SafeCDiag(C->getExprLoc(), "atomic-cast");
    // `FILE *f = malloc(...)`: nothing but the library makes a FILE.
    if (C->getType()->isPointerType() &&
        isLibraryHandle(C->getType()->getPointeeType()) &&
        C->getSubExpr()->getType()->isVoidPointerType() &&
        !C->getSubExpr()->isNullPointerConstant(
            S.Context, Expr::NPC_ValueDependentIsNotNull))
      S.SafeCDiag(C->getExprLoc(), "free-handle")
          << C->getType()->getPointeeType();
    collectVoid();
    QualType To = C->getType();
    // (Bytes from the allocator, through void, are aligned for anything.)
    if (QualType F = C->getSubExpr()->getType();
        C->getCastKind() == CK_BitCast && To->isPointerType() &&
        F->isPointerType() && !F->isVoidPointerType() &&
        !To->isVoidPointerType() && !To->isFunctionPointerType() &&
        !To->getPointeeType()->isIncompleteType() &&
        !F->getPointeeType()->isIncompleteType() &&
        S.Context.getTypeAlignInChars(To->getPointeeType()) >
            S.Context.getTypeAlignInChars(F->getPointeeType()))
      S.SafeCDiag(C->getExprLoc(), "cast-align")
          << To->getPointeeType() << F->getPointeeType();
    if (!To->isVoidPointerType() || To->getPointeeType().isConstQualified() ||
        VoidOk.contains(C))
      return true;
    QualType From = C->getSubExpr()->getType();
    if (!From->isPointerType() || From->isVoidPointerType() ||
        From->isFunctionPointerType())
      return true;
    // A pointer to nothing (null) or to pointer-free data is harmless.
    QualType T = From->getPointeeType();
    if (!S.isSafeCPlainData(S.Context, T, /*VoidIsPlain=*/true))
      S.SafeCDiag(C->getExprLoc(), "void-over-pointers") << T;
    return true;
  }

  bool VisitVarDecl(VarDecl *VD) {
    if (VD->hasAttr<FormatAttr>())
      S.SafeCDiag(VD->getLocation(), "format-pointer");
    if (VD->isLocalVarDecl() && S.Context.getAsVariableArrayType(VD->getType())) {
      // Leaving its block also gives back what 'alloca' took in it.
      SawVLA = VD->getLocation();
      if (SawAlloca.isValid())
        S.SafeCDiag(SawAlloca, "alloca");
    }
    if (VD->getType()->isPointerType() && !isa<ParmVarDecl>(VD) &&
        VD->getType()->getPointeeType()->isVariablyModifiedType())
      S.SafeCDiag(VD->getLocation(), "vla-pointer") << VD->getType();
    if (isLibraryHandle(VD->getType()))
      S.SafeCDiag(VD->getLocation(), "library-field") << VD->getType();
    if (const auto *VLA = S.Context.getAsVariableArrayType(VD->getType());
        VLA && VD->isLocalVarDecl() &&
        !S.isSafeCPlainData(S.Context, VLA->getElementType(), true))
      S.SafeCDiag(VD->getLocation(), "vla-pointers") << VLA->getElementType();
    if (const Expr *Init = VD->getInit(); Init && !isa<InitListExpr>(Init))
      checkStore(VD->getType(), Init);
    return true;
  }

  bool VisitSwitchStmt(SwitchStmt *SS) {
    // M4 rule 4: a switch on a union's tag handles every tag value.
    const auto *ME = dyn_cast<MemberExpr>(SS->getCond()->IgnoreParenImpCasts());
    const auto *TagField = ME ? dyn_cast<FieldDecl>(ME->getMemberDecl()) : nullptr;
    if (!TagField)
      return true;
    llvm::SmallVector<llvm::APSInt, 8> Cases;
    for (const SwitchCase *C = SS->getSwitchCaseList(); C;
         C = C->getNextSwitchCase()) {
      if (isa<DefaultStmt>(C))
        return true;
      if (const auto *CS = dyn_cast<CaseStmt>(C))
        if (std::optional<llvm::APSInt> V =
                CS->getLHS()->getIntegerConstantExpr(S.Context))
          Cases.push_back(*V);
    }
    for (const FieldDecl *F : TagField->getParent()->fields()) {
      if (taggedByName(F->getType()) != TagField->getName())
        continue;
      auto Require = [&](llvm::APSInt V, std::string Name) {
        for (const llvm::APSInt &C : Cases)
          if (llvm::APSInt::isSameValue(C, V))
            return;
        S.SafeCDiag(SS->getSwitchLoc(), "switch-not-exhaustive")
            << TagField->getName() << Name;
      };
      // An enum tag's every enumerator is a variant, with or without a member
      // holding data (Rust's unit variants); an integer tag's variants are
      // the values its members name.
      if (const auto *ET = TagField->getType()->getAs<EnumType>()) {
        for (const EnumConstantDecl *E :
             ET->getOriginalDecl()->getDefinitionOrSelf()->enumerators())
          Require(E->getInitVal(), E->getName().str());
      } else {
        whenValues(S.Context, F->getType()->getAsRecordDecl(),
                   [&](const FieldDecl *, llvm::APSInt V, SourceLocation) {
                     Require(V, toString(V, 10));
                   });
      }
      break;
    }
    return true;
  }

  /// Does E name a character of a terminated string?
  static bool inString(const Expr *E) {
    E = E->IgnoreParens();
    const Expr *P = nullptr;
    if (const auto *U = dyn_cast<UnaryOperator>(E);
        U && U->getOpcode() == UO_Deref)
      P = U->getSubExpr();
    else if (const auto *A = dyn_cast<ArraySubscriptExpr>(E))
      P = A->getBase();
    if (!P)
      return false;
    return P->getType()->isValueTerminatedType() ||
           safec::peel(P)->getType()->isValueTerminatedType();
  }
  bool VisitUnaryOperator(UnaryOperator *UO) {
    StringRef Tag;
    if (UO->isIncrementDecrementOp() && inString(UO->getSubExpr()))
      S.SafeCDiag(UO->getOperatorLoc(), "terminator-step");
    if (UO->getOpcode() == UO_AddrOf)
      if (const auto *D = dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParens());
          D && D->getDecl()->hasAttr<DependerDeclsAttr>() &&
          !isa<ParmVarDecl>(D->getDecl()))
        S.SafeCDiag(UO->getOperatorLoc(), "count-address") << D->getDecl();
    VisitGuardedAddress(UO);
    if (UO->getOpcode() == UO_AddrOf)
      checkNestedCount(UO->getSubExpr(), /*Address=*/true);
    if (UO->isIncrementDecrementOp())
      checkNestedCount(UO->getSubExpr());
    if (UO->getOpcode() == UO_AddrOf || UO->isIncrementDecrementOp())
      if (const auto *ME = dyn_cast<MemberExpr>(UO->getSubExpr()->IgnoreParens()))
        if (const auto *F = dyn_cast<FieldDecl>(ME->getMemberDecl());
            F && isTagField(F))
          S.SafeCDiag(UO->getOperatorLoc(), "tag-address") << F;
    if (UO->getOpcode() == UO_AddrOf)
      if (const FieldDecl *M = pointerUnionMember(S.Context, UO->getSubExpr(), Tag))
        S.SafeCDiag(UO->getOperatorLoc(), "tagged-member-address")
            << M << Tag << M->getName();
    return true;
  }

  SourceLocation SawAlloca, SawVLA;
  void checkOutCall(const CallExpr *Call) {
    const FunctionDecl *F = Call->getDirectCallee();
    if (!F || !F->getIdentifier())
      return;
    StringRef N = F->getName();
    N.consume_front("__builtin_");
    if (F->getBuiltinID() && N.starts_with("alloca")) {
      SawAlloca = Call->getExprLoc();
      Expr::EvalResult R;
      if (!Call->getNumArgs() || !Call->getArg(0)->EvaluateAsInt(R, S.Context) ||
          R.Val.getInt().ugt(1 << 20) || SawVLA.isValid())
        S.SafeCDiag(SawAlloca, "alloca");
      return;
    }
    // An out pointer the compiler or library writes whole: only a plain local,
    // which is not null, not out of bounds and not the count of a pointer.
    // Builtins that check their own arguments never reach the list in
    // SemaChecking.cpp.
    if (unsigned ID = F->getBuiltinID();
        ID && S.Context.BuiltinInfo.hasCustomTypechecking(ID) &&
        F->getName().starts_with("__builtin_") && !N.ends_with("_overflow") &&
        !N.starts_with("safec_") && !N.starts_with("va_") &&
        !N.starts_with("get_pointer_") &&
        !llvm::is_contained({"constant_p", "choose_expr", "classify_type",
                             "types_compatible_p", "expect", "launder",
                             "object_size", "dynamic_object_size"},
                            N) &&
        llvm::any_of(Call->arguments(), [](const Expr *A) {
          return A->getType()->isAnyPointerType() || A->getType()->isArrayType();
        }))
      S.SafeCDiag(Call->getExprLoc(), "builtin-unchecked") << F;
    if (!(F->getBuiltinID() && N.ends_with("_overflow")) &&
        !(F->isExternC() &&
          llvm::is_contained({"frexp", "frexpf", "frexpl", "modf", "modff",
                              "modfl", "remquo", "remquof", "remquol"},
                             N)))
      return;
    const Expr *Out = Call->getNumArgs()
                          ? safec::peel(
                                Call->getArg(Call->getNumArgs() - 1))
                          : nullptr;
    const auto *UO = dyn_cast_or_null<UnaryOperator>(
        Out ? Out->IgnoreParenCasts() : nullptr);
    const auto *DRE =
        UO && UO->getOpcode() == UO_AddrOf
            ? dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParens())
            : nullptr;
    const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
    if (!VD || !VD->hasLocalStorage() || VD->hasAttr<DependerDeclsAttr>() ||
        !VD->getType()->isArithmeticType())
      S.SafeCDiag(Out ? Out->getExprLoc() : Call->getExprLoc(), "out-arg") << F;
  }
  /// What was written, inside what bounds safety adds.
  static const Expr *plain(const Expr *E) {
    for (;;) {
      E = safec::peel(E)->IgnoreParenCasts();
      if (const auto *P = dyn_cast<BoundsSafetyPointerPromotionExpr>(E))
        E = P->getPointer();
      else if (const auto *M = dyn_cast<MaterializeSequenceExpr>(E))
        E = M->getWrappedExpr();
      else if (const auto *B = dyn_cast<BoundsCheckExpr>(E))
        E = B->getGuardedExpr();
      else if (const auto *T = dyn_cast<TerminatedByFromIndexableExpr>(E))
        E = T->getPointer();
      else
        return E;
    }
  }
  /// The variable an array or pointer argument names.
  static const VarDecl *named(const Expr *E) {
    E = plain(E);
    const auto *D = dyn_cast<DeclRefExpr>(E);
    return D ? dyn_cast<VarDecl>(D->getDecl()) : nullptr;
  }
  static bool isOwner(const VarDecl *V) {
    return V->hasLocalStorage() && safeCPointerKind(V->getType()) == "owned";
  }
  /// Does everything V is set from come from locals and fresh memory, so that
  /// no global is or can reach it?
  bool onlyLocal(const VarDecl *V) {
    llvm::SmallPtrSet<const VarDecl *, 8> Seen;
    return onlyLocal(V, Seen);
  }
  bool onlyLocal(const VarDecl *V, llvm::SmallPtrSetImpl<const VarDecl *> &Seen) {
    if (!V->hasLocalStorage())
      return false;
    if (!Seen.insert(V).second)
      return true; // being looked at
    if (V->getType()->isArrayType() || isOwner(V))
      return true;
    if (isa<ParmVarDecl>(V))
      return false;
    struct From : RecursiveASTVisitor<From> {
      PointerKindChecker *C;
      llvm::SmallPtrSetImpl<const VarDecl *> *Seen;
      const VarDecl *V;
      bool Ok = true;
      bool VisitDeclRefExpr(DeclRefExpr *D) {
        if (const auto *O = dyn_cast<VarDecl>(D->getDecl()))
          if (O != V && (O->getType()->isAnyPointerType() ||
                         O->getType()->isArrayType() || O->getType()->isRecordType()))
            Ok &= C->onlyLocal(O, *Seen);
        return true;
      }
      bool VisitMemberExpr(MemberExpr *M) {
        if (M->getType()->isAnyPointerType())
          Ok = false;
        return true;
      }
      bool VisitOpaqueValueExpr(OpaqueValueExpr *O) {
        return !O->getSourceExpr() || TraverseStmt(O->getSourceExpr());
      }
    } F;
    F.C = this;
    F.Seen = &Seen;
    F.V = V;
    if (V->getInit())
      F.TraverseStmt(const_cast<Expr *>(V->getInit()));
    struct Assigns : RecursiveASTVisitor<Assigns> {
      From *F;
      bool VisitBinaryOperator(BinaryOperator *B) {
        const auto *D = dyn_cast<DeclRefExpr>(B->getLHS()->IgnoreParens());
        if (B->isAssignmentOp() && D && D->getDecl() == F->V)
          F->TraverseStmt(B->getRHS());
        return true;
      }
    } A;
    A.F = &F;
    if (Body)
      A.TraverseStmt(const_cast<Stmt *>(Body));
    return F.Ok && Body;
  }
  /// A buffer passed as a string for one call: nothing changes it in between.
  void checkStrings(const CallExpr *Call) {
    const FunctionDecl *F = Call->getDirectCallee();
    for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
      const Expr *E = Call->getArg(I);
      const TerminatedByFromIndexableExpr *T = nullptr;
      for (const Expr *X = E; X && !T;) {
        T = dyn_cast<TerminatedByFromIndexableExpr>(X);
        const Expr *N = X->IgnoreParenImpCasts();
        if (const auto *O = dyn_cast<OpaqueValueExpr>(N))
          N = O->getSourceExpr();
        X = N == X ? nullptr : N;
        if (X)
          T = dyn_cast<TerminatedByFromIndexableExpr>(X);
      }
      if (!T)
        continue;
      // strtol's end is in the buffer; as a string it would outlive the check.
      if (F && F->isExternC() && F->getIdentifier() &&
          (F->getName().starts_with("strto") || F->getName().starts_with("wcsto")) &&
          Call->getNumArgs() > 1) {
        const auto *U = dyn_cast<UnaryOperator>(plain(Call->getArg(1)));
        if (U && U->getOpcode() == UO_AddrOf &&
            U->getSubExpr()->getType()->isValueTerminatedType())
          S.SafeCDiag(T->getExprLoc(), "string-result");
      }
      const VarDecl *Buf = named(T->getPointer());
      bool Local = Buf && Buf->hasLocalStorage() && Buf->getType()->isArrayType();
      bool System = F && S.getSourceManager().isInSystemHeader(F->getLocation());
      if (Call->getType()->isAnyPointerType() &&
          !Call->getType()->isVoidPointerType() &&
          Call->getType()->getPointeeType()->isAnyCharacterType() && System &&
          !llvm::is_contained({"strdup", "strndup", "getenv", "strcpy", "strcat"},
                              F->getName()))
        S.SafeCDiag(T->getExprLoc(), "string-result");
      StringRef Kind = safeCPointerKind(Call->getType());
      // (A borrow a function returns is from its first borrowed parameter.)
      bool First = true;
      for (unsigned J = 0; F && J < I && J < F->getNumParams(); ++J) {
        QualType T = F->getParamDecl(J)->getType();
        StringRef K = safeCPointerKind(T);
        First &= !(T->isRecordType() ||
                   (T->isAnyPointerType() && K != "owned" && K != "static" &&
                    K != "unchecked"));
      }
      if (!System && First && Kind != "owned" && Kind != "static" && Kind != "unchecked" &&
          (Call->getType()->isValueTerminatedType() ||
           Call->getType()->isRecordType()))
        S.SafeCDiag(T->getExprLoc(), "string-result");
      for (unsigned J = 0; J < Call->getNumArgs(); ++J) {
        if (J == I)
          continue;
        const Expr *O = Call->getArg(J);
        Effects Fx;
        Fx.TraverseStmt(const_cast<Expr *>(O));
        struct Sets : RecursiveASTVisitor<Sets> {
          bool Any = false;
          bool VisitBinaryOperator(BinaryOperator *B) {
            Any |= B->isAssignmentOp();
            return true;
          }
          bool VisitUnaryOperator(UnaryOperator *U) {
            Any |= U->isIncrementDecrementOp();
            return true;
          }
        } Set;
        Set.TraverseStmt(const_cast<Expr *>(O));
        struct Calls : RecursiveASTVisitor<Calls> {
          Sema *S;
          bool Ours = false;
          bool VisitCallExpr(CallExpr *C) {
            const FunctionDecl *D = C->getDirectCallee();
            Ours |= !D || (!D->getBuiltinID() &&
                           !S->getSourceManager().isInSystemHeader(D->getLocation()));
            return true;
          }
        } Cs;
        Cs.S = &S;
        Cs.TraverseStmt(const_cast<Expr *>(O));
        bool Reach = Cs.Ours && !Local && !(Buf && onlyLocal(Buf));
        if (Fx.Call || Set.Any || Reach)
          S.SafeCDiag(O->getExprLoc(), "string-alias") << "this argument";
        // (printf only reads its data.)
        if (const auto *FA = F ? F->getAttr<FormatAttr>() : nullptr;
            FA && J >= F->getNumParams() && !F->getName().contains("scanf"))
          continue;
        QualType OT = O->getType();
        QualType To = OT->isAnyPointerType() ? OT->getPointeeType() : QualType();
        if (F && J < F->getNumParams() && F->getParamDecl(J)->getType()->isAnyPointerType())
          To = F->getParamDecl(J)->getType()->getPointeeType();
        if (To.isNull() || To.isConstQualified() || To->isFunctionType())
          continue;
        const VarDecl *Other = named(O);
        bool Apart = Other && Buf && Other != Buf &&
                     ((Buf->getType()->isArrayType() && Other->getType()->isArrayType()) ||
                      (Buf->getType()->isArrayType() && Local && isa<ParmVarDecl>(Other)) ||
                      (Other->getType()->isArrayType() && Other->hasLocalStorage() &&
                       isa<ParmVarDecl>(Buf)) ||
                      (isOwner(Buf) != isOwner(Other)) ||
                      (isOwner(Buf) && isOwner(Other)));
        QualType Elem = T->getType()->getPointeeType();
        bool Bytes = To->isAnyCharacterType() || (System && To->isVoidType()) ||
                     S.Context.hasSameUnqualifiedType(To, Elem);
        if (Bytes && !Apart)
          S.SafeCDiag(O->getExprLoc(), "string-alias") << "this argument";
      }
      // A function of this program can reach a global, and what a parameter
      // points to.
      if (!System && !Local && !(Buf && onlyLocal(Buf)))
        S.SafeCDiag(T->getExprLoc(), "string-alias") << "the function";
    }
  }
  /// printf's format is written in the call; scanf writes plain variables.
  void checkFormats(const CallExpr *Call) {
    const FunctionDecl *F = Call->getDirectCallee();
    const auto *FA = F ? F->getAttr<FormatAttr>() : nullptr;
    if (!FA || FA->getFirstArg() == 0 || !FA->getFormatIdx() ||
        FA->getFormatIdx() > int(Call->getNumArgs()))
      return;
    const Expr *Fmt = plain(Call->getArg(FA->getFormatIdx() - 1));
    const auto *SL = dyn_cast<StringLiteral>(Fmt->IgnoreParenImpCasts());
    if (!SL || !SL->isOrdinary())
      S.SafeCDiag(Fmt->getExprLoc(), "printf-format");
    if (!F->getIdentifier() || !F->getName().contains("scanf"))
      return;
    for (unsigned I = FA->getFirstArg() - 1; I < Call->getNumArgs(); ++I) {
      const Expr *A = safec::peel(Call->getArg(I))->IgnoreParenImpCasts();
      const VarDecl *V = nullptr;
      if (const auto *U = dyn_cast<UnaryOperator>(A);
          U && U->getOpcode() == UO_AddrOf) {
        V = named(U->getSubExpr());
        if (V && !V->getType()->isArithmeticType())
          V = nullptr;
      } else {
        V = named(A);
        if (V && (!V->getType()->isConstantArrayType() ||
                  V->getType()->isValueTerminatedType()))
          V = nullptr;
      }
      if (!V || !V->hasLocalStorage() || V->hasAttr<DependerDeclsAttr>())
        S.SafeCDiag(A->getExprLoc(), "scanf-plain");
    }
  }
  /// What fgets or getcwd returns is the buffer, which can still be changed: it
  /// is only tested or ignored.
  llvm::SmallPtrSet<const Expr *, 8> Tested;
  llvm::SmallPtrSet<const Stmt *, 4> Values;
  void tested(const Stmt *St) {
    if (const auto *E = dyn_cast_or_null<Expr>(St); E && !Values.contains(St))
      Tested.insert(plain(E));
  }
  void markTested(const Stmt *St) {
    if (const auto *X = dyn_cast<StmtExpr>(St)) {
      if (!X->getSubStmt()->body_empty())
        Values.insert(X->getSubStmt()->body_back());
    } else if (const auto *C = dyn_cast<CompoundStmt>(St)) {
      for (const Stmt *B : C->body())
        tested(B);
    } else if (const auto *I = dyn_cast<IfStmt>(St)) {
      tested(I->getCond()), tested(I->getThen()), tested(I->getElse());
    } else if (const auto *W = dyn_cast<WhileStmt>(St)) {
      tested(W->getCond()), tested(W->getBody());
    } else if (const auto *D = dyn_cast<DoStmt>(St)) {
      tested(D->getCond()), tested(D->getBody());
    } else if (const auto *F = dyn_cast<ForStmt>(St)) {
      tested(F->getInit()), tested(F->getCond()), tested(F->getInc()),
          tested(F->getBody());
    } else if (const auto *L = dyn_cast<LabelStmt>(St)) {
      tested(L->getSubStmt());
    } else if (const auto *C = dyn_cast<SwitchCase>(St)) {
      tested(C->getSubStmt());
    } else if (const auto *Q = dyn_cast<ConditionalOperator>(St)) {
      tested(Q->getCond());
    } else if (const auto *C = dyn_cast<CStyleCastExpr>(St)) {
      if (C->getType()->isVoidType())
        tested(C->getSubExpr());
    } else if (const auto *U = dyn_cast<UnaryOperator>(St)) {
      if (U->getOpcode() == UO_LNot)
        tested(U->getSubExpr());
    } else if (const auto *B = dyn_cast<BinaryOperator>(St)) {
      if (B->isLogicalOp() || B->isEqualityOp())
        tested(B->getLHS()), tested(B->getRHS());
    }
  }
  void checkFgets(const CallExpr *C) {
    const FunctionDecl *F = C->getDirectCallee();
    if (!F || Tested.contains(C) || !F->isExternC() ||
        !C->getType()->isValueTerminatedType() ||
        !S.getSourceManager().isInSystemHeader(F->getLocation()))
      return;
    // Any library function given a buffer to write may return it.
    for (const ParmVarDecl *P : F->parameters())
      if (P->getFunctionScopeIndex() < C->getNumArgs() &&
          !plain(C->getArg(P->getFunctionScopeIndex()))
               ->isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNotNull) &&
          P->getType()->isAnyPointerType() &&
          P->getType()->getPointeeType()->isAnyCharacterType() &&
          !P->getType()->getPointeeType().isConstQualified() &&
          !P->getType()->isValueTerminatedType()) {
        S.SafeCDiag(C->getExprLoc(), "string-result");
        return;
      }
  }
  bool VisitCallExpr(CallExpr *Call) {
    checkFgets(Call);
    if (const FunctionDecl *F = Call->getDirectCallee();
        F && F->isExternC() && F->getIdentifier() && Call->getNumArgs() > 1 &&
        (F->getName().starts_with("strto") || F->getName().starts_with("wcsto")) &&
        S.getSourceManager().isInSystemHeader(F->getLocation()) &&
        F->getParamDecl(1)->getType()->isPointerType() &&
        F->getParamDecl(1)->getType()->getPointeeType()->isPointerType() &&
        !plain(Call->getArg(1))
             ->isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNotNull)) {
      // What the argument was before it became a 'const char *'.
      const Expr *A = plain(Call->getArg(0));
      QualType T = A->getType();
      QualType E = T->isPointerType() ? T->getPointeeType()
                   : T->isArrayType()
                       ? QualType(T->getPointeeOrArrayElementType(), 0)
                       : QualType();
      const auto *AT = T->getAsArrayTypeUnsafe();
      if (E.isNull() || E.isConstQualified() || T.isConstQualified() ||
          (AT && AT->getElementType().isConstQualified()) || isa<StringLiteral>(A))
        S.SafeCDiag(Call->getExprLoc(), "const-end") << F;
    }
    checkStrings(Call);
    checkFormats(Call);
    for (unsigned I = 0; I < Call->getNumArgs(); ++I)
      if (Call->getArg(I)->getType()->isPointerTypeWithBounds())
        for (unsigned J = 0; J < Call->getNumArgs(); ++J)
          if (J != I)
            checkOrder(Call->getArg(I), Call->getArg(J),
                       Call->getArg(I)->getExprLoc());
    if (Call->getBuiltinCallee() == Builtin::BI__builtin_safec_take &&
        Call->getNumArgs() == 1)
      if (const auto *ME = dyn_cast<MemberExpr>(Call->getArg(0)->IgnoreParens()))
        if (const auto *CAT = ME->getType()->getAs<CountAttributedType>())
          for (const TypeCoupledDeclRefInfo &D : CAT->getCoupledDecls())
            if (const auto *Dep = D.getDecl()->getAttr<DependerDeclsAttr>();
                Dep && Dep->dependerDecls_size() > 1)
              // Zero is the smallest of '__counted_by(n)', not of '(4 - n)'.
              for (const Decl *Sib : Dep->dependerDecls()) {
                const auto *V = dyn_cast<ValueDecl>(Sib);
                const auto *SC =
                    V ? V->getType()->getAs<CountAttributedType>() : nullptr;
                if (SC && !isa<DeclRefExpr>(SC->getCountExpr()->IgnoreParenImpCasts()))
                  S.SafeCDiag(Call->getExprLoc(), "take-shared-count")
                      << ME->getMemberDecl();
              }
    checkOutCall(Call);
    if (const FunctionDecl *F = Call->getDirectCallee();
        F && F->getIdentifier() && F->getName() == "free")
      for (const Expr *A : Call->arguments())
        while (true) {
          FlexMayConvert.insert(A);
          if (const auto *PE = dyn_cast<ParenExpr>(A))
            A = PE->getSubExpr();
          else if (const auto *IC = dyn_cast<ImplicitCastExpr>(A))
            A = IC->getSubExpr();
          else
            break;
        }
    if (const FunctionDecl *F = Call->getDirectCallee();
        F && F->getIdentifier() && F->getName().starts_with("__sync_"))
      S.SafeCDiag(Call->getExprLoc(), "builtin-unchecked-name") << F->getName();
    if (Call->getCallee()->getType()->isBlockPointerType())
      S.SafeCDiag(Call->getExprLoc(), "block");
    if (const FunctionDecl *F = Call->getDirectCallee())
      checkVaList(Call, F);
    // FILE and DIR are the library's: never freed as memory, never allocated.
    if (const FunctionDecl *F = Call->getDirectCallee();
        F && F->isExternC() && F->getIdentifier() && Call->getNumArgs() >= 1 &&
        llvm::is_contained({"free", "realloc", "reallocf"}, F->getName())) {
      QualType T = safec::peel(Call->getArg(0))
                       ->IgnoreParenCasts()
                       ->getType();
      if (T->isPointerType() && isLibraryHandle(T->getPointeeType()))
        S.SafeCDiag(Call->getExprLoc(), "free-handle") << T->getPointeeType();
    }
    return true;
  }

  bool VisitImplicitCastExpr(ImplicitCastExpr *ICE) {
    if (ICE->getCastKind() == CK_ArrayToPointerDecay) {
      StringRef Tag;
      if (const FieldDecl *M = pointerUnionMember(S.Context, ICE->getSubExpr(), Tag))
        S.SafeCDiag(ICE->getExprLoc(), "tagged-member-address")
            << M << Tag << M->getName();
      return true;
    }
    return true;
  }
};
} // namespace

void clang::checkSafeCPointerKindsIn(Sema &S, Stmt *Body) {
  if (Body) {
    PointerKindChecker C(S, Body);
    if (const FunctionDecl *F = S.getCurFunctionDecl())
      for (const ParmVarDecl *P : F->parameters()) {
        C.checkParam(P);
        if (const auto *CAT = P->getType()->getAs<CountAttributedType>())
          for (const TypeCoupledDeclRefInfo &D : CAT->getCoupledDecls())
            if (D.isDeref())
              S.SafeCDiag(P->getLocation(), "count-behind");
        if (F->isMain()) {
          const auto *CAT = P->getType()->getAs<CountAttributedType>();
          const auto *N = CAT ? dyn_cast<DeclRefExpr>(CAT->getCountExpr()->IgnoreParenImpCasts()) : nullptr;
          if (CAT && (!N || N->getDecl() != F->getParamDecl(0) || P->getFunctionScopeIndex() != 1))
            S.SafeCDiag(P->getLocation(), "main-count");
        }
      }
    C.TraverseStmt(Body);
  }
}

void clang::checkSafeCPointerKindsIn(Sema &S, Decl *GlobalVar) {
  if (GlobalVar)
    PointerKindChecker(S).TraverseDecl(GlobalVar);
}

bool clang::checkSafeCTakeBuiltin(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCount(TheCall, 1))
    return true;
  Expr *Arg = TheCall->getArg(0);
  QualType T = Arg->getType();
  if (!T->isPointerType() ||
      Arg->isModifiableLvalue(S.Context) != Expr::MLV_Valid) {
    S.SafeCDiag(Arg->getExprLoc(), "take-operand");
    return true;
  }
  // Taking leaves null behind, so the place must allow it.
  if (T->getNullability() != NullabilityKind::Nullable &&
      S.getLangOpts().NullabilitySafety) {
    const ValueDecl *D = namedStorage(Arg);
    S.SafeCDiag(Arg->getExprLoc(), "take-nonnull") << (D ? D->getName() : "");
    return true;
  }
  // The result is a plain pointer: a count field belongs to the struct, so
  // it cannot travel with the value.
  if (const auto *CAT = T->getAs<CountAttributedType>())
    T = CAT->desugar();
  TheCall->setType(T.getUnqualifiedType());
  return false;
}

namespace {
/// A tag place: the shared member path type (also used by the nullability
/// analysis), so tag and null facts are keyed the same way.
using TagPath = MemberAccessPath;

/// The path of a struct expression: a variable itself (no fields), or a
/// member chain (`p->a`, `(*p).a`, `v.b.c`).
static std::optional<TagPath> structPath(const Expr *E) {
  if (const auto *DRE =
          dyn_cast<DeclRefExpr>(ignoreBoundsSafetyWrappers(E)))
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      return TagPath{VD, {}};
  if (const auto *UO =
          dyn_cast<UnaryOperator>(ignoreBoundsSafetyWrappers(E));
      UO && UO->getOpcode() == UO_Deref)
    return structPath(UO->getSubExpr());
  return decomposeMemberAccess(E);
}

/// The values a tag may hold on every path here (a case list can give
/// several, as `case RECT: case SQUARE:` does).
/// Written is false after a tag write until the member it names is written:
/// until then the union still holds the old member's bytes.
struct TagValues : llvm::SmallVector<llvm::APSInt, 2> {
  using llvm::SmallVector<llvm::APSInt, 2>::SmallVector;
  bool Written = true;
};
using TagFacts = llvm::SmallVector<std::pair<TagPath, TagValues>, 4>;

static const TagValues *lookupFact(const TagFacts &F, const TagPath &P) {
  for (const auto &[K, V] : F)
    if (K == P)
      return &V;
  return nullptr;
}
static void setFact(TagFacts &F, const TagPath &P,
                    std::optional<llvm::APSInt> V, bool Written = true) {
  llvm::erase_if(F, [&](const auto &KV) { return KV.first == P; });
  if (V || !Written) {
    // No value: the tag is unknown, but an unwritten member stays known.
    TagValues Vs;
    if (V)
      Vs.push_back(*V);
    Vs.Written = Written;
    F.push_back({P, Vs});
  }
}
// A tag changed somewhere this analysis cannot see the member written: the
// tag is unknown and the member must be written before it is read.
static void unwriteIf(TagFacts &F,
                      llvm::function_ref<bool(const TagPath &)> Pred) {
  for (auto &KV : F)
    if (Pred(KV.first)) {
      KV.second.clear();
      KV.second.Written = false;
    }
}
static bool hasValue(const TagValues &Vs, const llvm::APSInt &V) {
  return llvm::any_of(Vs, [&](const llvm::APSInt &W) {
    return llvm::APSInt::isSameValue(W, V);
  });
}
// Forgetting a tag keeps one thing: that its member was not written since
// the tag last changed. An empty value set is "tag unknown"; a later test
// narrows it again but must not make the member look written (the union
// still holds the old member's bytes).
static void forgetFact(TagFacts &F, const TagPath &P) {
  for (auto I = F.begin(); I != F.end(); ++I)
    if (I->first == P) {
      if (I->second.Written)
        F.erase(I);
      else
        I->second.clear();
      return;
    }
}
static void killRoot(TagFacts &F, const ValueDecl *Root) {
  llvm::SmallVector<TagPath, 2> Paths;
  for (const auto &KV : F)
    if (KV.first.Root == Root)
      Paths.push_back(KV.first);
  for (const TagPath &P : Paths)
    forgetFact(F, P);
}
// A tag test narrows the values the tag may hold; whether the member was
// written since the tag changed stays as it was.
static void narrowFact(TagFacts &F, const TagPath &P, llvm::APSInt V) {
  const TagValues *Old = lookupFact(F, P);
  setFact(F, P, V, Old ? Old->Written : true);
}

/// A tag field: an integer field whose struct has a union tagged by it.
static bool isTagField(const FieldDecl *F) {
  // An unnamed field (an anonymous union) is never a tag; without this its
  // empty name would match the empty name of a non-union sibling.
  if (F->getName().empty())
    return false;
  for (const FieldDecl *Sib : F->getParent()->fields())
    if (taggedByName(Sib->getType()) == F->getName())
      return true;
  return false;
}

/// For `x.tag == C` (either order, or !=): the tag path, constant, and
/// whether the comparison is ==.
static bool matchTagTest(const ASTContext &Ctx, const Expr *Cond, TagPath &P,
                         llvm::APSInt &V, bool &Eq) {
  Cond = Cond->IgnoreParenImpCasts();
  if (const auto *UO = dyn_cast<UnaryOperator>(Cond);
      UO && UO->getOpcode() == UO_LNot) {
    if (!matchTagTest(Ctx, UO->getSubExpr(), P, V, Eq))
      return false;
    Eq = !Eq;
    return true;
  }
  // A bare tag as a condition (`if (r.ok)`) tests it against true.
  if (const auto *ME = dyn_cast<MemberExpr>(Cond))
    if (const auto *F = dyn_cast<FieldDecl>(ME->getMemberDecl());
        F && isTagField(F) && F->getType()->isBooleanType())
      if (std::optional<TagPath> Path = structPath(ME)) {
        P = *Path;
        V = llvm::APSInt(llvm::APInt(Ctx.getIntWidth(F->getType()), 1),
                         /*isUnsigned=*/true);
        Eq = true;
        return true;
      }
  const auto *BO = dyn_cast<BinaryOperator>(Cond);
  if (!BO || !BO->isEqualityOp())
    return false;
  for (auto [A, B] : {std::pair(BO->getLHS(), BO->getRHS()),
                      std::pair(BO->getRHS(), BO->getLHS())}) {
    const auto *ME = dyn_cast<MemberExpr>(A->IgnoreParenImpCasts());
    const auto *F = ME ? dyn_cast<FieldDecl>(ME->getMemberDecl()) : nullptr;
    std::optional<llvm::APSInt> C = B->getIntegerConstantExpr(Ctx);
    if (!F || !C || !isTagField(F))
      continue;
    std::optional<TagPath> Path = structPath(A);
    if (!Path)
      continue;
    P = *Path;
    V = *C;
    Eq = BO->getOpcode() == BO_EQ;
    return true;
  }
  return false;
}

/// Whether a fact about \p Root can be changed through some other path: a
/// parameter or global, a pointer, or a local whose address was taken.
static bool mayBeAliased(const VarDecl *Root,
                         const llvm::DenseSet<const VarDecl *> &AddrTaken) {
  return !Root || isa<ParmVarDecl>(Root) || Root->hasGlobalStorage() ||
         Root->getType()->isPointerType() || AddrTaken.count(Root);
}

/// Updates facts for one statement (assignments to tags, kills).
static void transfer(const ASTContext &Ctx, const Stmt *St, TagFacts &F,
                     const llvm::DenseSet<const VarDecl *> &AddrTaken) {
  // `struct S s = {TAG, ...}`: an initializer sets each tag it names.
  if (const auto *DS = dyn_cast<DeclStmt>(St)) {
    for (const Decl *D : DS->decls())
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        killRoot(F, VD);
        const auto *ILE =
            VD->getInit()
                ? dyn_cast<InitListExpr>(VD->getInit()->IgnoreParenImpCasts())
                : nullptr;
        const RecordDecl *RD = VD->getType()->getAsRecordDecl();
        if (!ILE || !RD)
          continue;
        if (const InitListExpr *Sem = ILE->getSemanticForm())
          ILE = Sem;
        unsigned I = 0;
        for (const FieldDecl *Fld : RD->fields()) {
          if (Fld->isUnnamedBitField())
            continue;
          if (I >= ILE->getNumInits())
            break;
          const Expr *Init = ILE->getInit(I++);
          if (Init && isTagField(Fld))
            setFact(F, TagPath{VD, {Fld}}, Init->getIntegerConstantExpr(Ctx));
        }
      }
    return;
  }
  if (const auto *BO = dyn_cast<BinaryOperator>(St); BO && BO->isAssignmentOp()) {
    const Expr *L = BO->getLHS()->IgnoreParenImpCasts();
    if (const auto *ME = dyn_cast<MemberExpr>(L))
      if (const auto *Fld = dyn_cast<FieldDecl>(ME->getMemberDecl());
          Fld && isTagField(Fld))
        if (std::optional<TagPath> P = structPath(L)) {
          // The same tag of another struct may be this one under another
          // name (`q = &x; q->kind = ...`): forget those.
          unwriteIf(F, [&](const TagPath &Q) {
            return Q.Fields.back() == Fld && Q.Root != P->Root &&
                   mayBeAliased(Q.Root, AddrTaken);
          });
          // The union still holds the old member's bytes until the newly
          // named member is written.
          setFact(F, *P,
                  BO->getOpcode() == BO_Assign
                      ? BO->getRHS()->getIntegerConstantExpr(Ctx)
                      : std::nullopt,
                  /*Written=*/false);
          return;
        } else {
          // A tag written through a place the analysis cannot name (a[j].k):
          // it may be any struct's tag of this kind.
          unwriteIf(F, [&](const TagPath &Q) { return Q.Fields.back() == Fld; });
          return;
        }
    // Reassigning a variable, or a struct holding a tag, forgets its tags.
    if (std::optional<TagPath> P = structPath(L))
      if (P->Fields.empty() || L->getType()->isRecordType())
        killRoot(F, P->Root);
    return;
  }
  if (const auto *UO = dyn_cast<UnaryOperator>(St);
      UO && UO->isIncrementDecrementOp())
    if (std::optional<TagPath> P = structPath(UO->getSubExpr()))
      forgetFact(F, *P);
  // A call may change any tag it can reach: through a modifiable pointer
  // argument, or through globals (any function not known to touch none).
  if (const auto *Call = dyn_cast<CallExpr>(St)) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    bool CallsBack = llvm::any_of(Call->arguments(), [](const Expr *A) {
      return A->getType()->isFunctionPointerType();
    });
    bool Library = Callee && (Call->getBuiltinCallee() ||
                              Ctx.getSourceManager().isInSystemHeader(
                                  Callee->getCanonicalDecl()->getLocation()));
    bool Pure = Callee && (Callee->hasAttr<PureAttr>() ||
                           Callee->hasAttr<ConstAttr>());
    bool Modifies = !(Pure || (Library && !CallsBack));
    for (const Expr *A : Call->arguments()) {
      QualType T = A->getType();
      if (!T->isPointerType() || T->getPointeeType().isConstQualified())
        continue;
      Modifies = true;
      if (const VarDecl *Root = safec::rootVar(A))
        killRoot(F, Root);
    }
    if (Modifies) {
      llvm::SmallVector<TagPath, 2> Paths;
      for (const auto &KV : F)
        if (mayBeAliased(KV.first.Root, AddrTaken))
          Paths.push_back(KV.first);
      for (const TagPath &P : Paths)
        forgetFact(F, P);
    }
  }
}

/// Facts on the edge from block B to successor index I (true = 0, false = 1
/// for an if; case labels for a switch).
static TagFacts edgeFacts(const ASTContext &Ctx, const CFGBlock &B,
                          unsigned I, const CFGBlock *Succ, TagFacts F) {
  const Stmt *Term = B.getTerminatorStmt();
  if (const Expr *Cond = B.getLastCondition(); Cond && !isa<SwitchStmt>(Term)) {
    TagPath P;
    llvm::APSInt V;
    bool Eq;
    if (matchTagTest(Ctx, Cond, P, V, Eq)) {
      if ((I == 0) == Eq)
        narrowFact(F, P, V);
      else if (P.Fields.back()->getType()->isBooleanType())
        // A bool that is not one value is the other.
        narrowFact(F, P, llvm::APSInt(llvm::APInt(V.getBitWidth(), V == 0),
                                      V.isUnsigned()));
    }
  }
  if (const auto *SS = dyn_cast_or_null<SwitchStmt>(Term); SS && Succ)
    if (const auto *CS = dyn_cast_or_null<CaseStmt>(Succ->getLabel()))
      if (const auto *ME =
              dyn_cast<MemberExpr>(SS->getCond()->IgnoreParenImpCasts()))
        if (const auto *Fld = dyn_cast<FieldDecl>(ME->getMemberDecl());
            Fld && isTagField(Fld) && !CS->getRHS())
          if (std::optional<TagPath> P = structPath(ME))
            if (std::optional<llvm::APSInt> V =
                    CS->getLHS()->getIntegerConstantExpr(Ctx))
              narrowFact(F, *P, *V);
  return F;
}

/// At a join: a tag stays known if both paths know it; it may then hold the
/// values either path allows.
static TagFacts meet(const TagFacts &A, const TagFacts &B) {
  TagFacts R;
  for (const auto &[K, V] : A)
    if (const TagValues *W = lookupFact(B, K)) {
      TagValues U;
      // Unknown on either path (an empty set) stays unknown.
      if (!V.empty() && !W->empty()) {
        U = V;
        for (const llvm::APSInt &X : *W)
          if (!hasValue(U, X))
            U.push_back(X);
      }
      U.Written = V.Written && W->Written;
      if (!U.empty() || !U.Written)
        R.push_back({K, U});
    } else if (!V.Written) {
      // Known on one path only: the tag is unknown, but an unwritten member
      // on that path must still be written before it is read.
      TagValues U;
      U.Written = false;
      R.push_back({K, U});
    }
  for (const auto &[K, W] : B)
    if (!lookupFact(A, K) && !W.Written) {
      TagValues U;
      U.Written = false;
      R.push_back({K, U});
    }
  return R;
}
static bool sameFacts(const TagFacts &A, const TagFacts &B) {
  if (A.size() != B.size())
    return false;
  for (const auto &[K, V] : A) {
    const TagValues *W = lookupFact(B, K);
    if (!W || W->size() != V.size() || W->Written != V.Written)
      return false;
  }
  return true;
}
} // namespace

/// `struct T x = {.tag = V, .m = ...}` must fill a member live for V.
static void checkTagInits(Sema &S, const DeclStmt *DS) {
  for (const Decl *D : DS->decls()) {
    const auto *VD = dyn_cast<VarDecl>(D);
    const Expr *VInit = VD && VD->getInit()
                            ? VD->getInit()->IgnoreParenImpCasts()
                            : nullptr;
    if (const auto *CLE = dyn_cast_or_null<CompoundLiteralExpr>(VInit))
      VInit = CLE->getInitializer()->IgnoreParenImpCasts();
    const auto *ILE = dyn_cast_or_null<InitListExpr>(VInit);
    const RecordDecl *RD = VD ? VD->getType()->getAsRecordDecl() : nullptr;
    if (!ILE || !RD)
      continue;
    if (const InitListExpr *Sem = ILE->getSemanticForm())
      ILE = Sem;
    std::optional<llvm::APSInt> TagV;
    const FieldDecl *TagF = nullptr;
    unsigned I = 0;
    llvm::SmallVector<std::pair<const FieldDecl *, const Expr *>, 4> Inits;
    for (const FieldDecl *F : RD->fields()) {
      if (F->isUnnamedBitField())
        continue;
      if (I >= ILE->getNumInits())
        break;
      Inits.push_back({F, ILE->getInit(I++)});
    }
    for (auto [F, Init] : Inits)
      if (Init && isTagField(F)) {
        TagF = F;
        TagV = Init->getIntegerConstantExpr(S.Context);
      }
    if (TagF && !TagV)
      // A tag known only at run time, with a member written beside it.
      for (auto [F, Init] : Inits)
        if (Init && taggedByName(F->getType()) == TagF->getName() &&
            isa<InitListExpr>(Init->IgnoreParenImpCasts()) &&
            !isa<ImplicitValueInitExpr>(Init))
          S.SafeCDiag(Init->getBeginLoc(), "tag-init-unknown") << TagF;
    if (!TagF || !TagV)
      continue;
    for (auto [F, Init] : Inits) {
      if (taggedByName(F->getType()) != TagF->getName() || !Init)
        continue;
      const auto *UILE = dyn_cast<InitListExpr>(Init->IgnoreParenImpCasts());
      if (!UILE)
        continue;
      if (const InitListExpr *Sem = UILE->getSemanticForm())
        UILE = Sem;
      const FieldDecl *Member = UILE->getInitializedFieldInUnion();
      if (!Member || UILE->getNumInits() == 0)
        continue;
      bool Live = false;
      whenValues(S.Context, F->getType()->getAsRecordDecl(),
                 [&](const FieldDecl *M, llvm::APSInt V, SourceLocation) {
                   if (M == Member && llvm::APSInt::isSameValue(V, *TagV))
                     Live = true;
                 });
      if (!Live)
        S.SafeCDiag(UILE->getBeginLoc(), "union-init-mismatch")
            << toString(*TagV, 10) << Member;
    }
  }
}

void clang::checkSafeCTaggedAccess(Sema &S, Decl *Fn, Stmt *Body) {
  if (!Fn || !Body)
    return;
  // Quick exit: no member of a tagged union is named in this body.
  struct Finder : RecursiveASTVisitor<Finder> {
    bool Any = false;
    Sema *S = nullptr;
    // `t.k = V;` is followed at once by a write of the member V selects:
    // until then whoever is given t trusts a tag that does not match.
    llvm::SmallPtrSet<const Expr *, 8> Paired;
    static const Expr *bare(const Stmt *St) {
      const auto *E = dyn_cast_or_null<Expr>(St);
      while (E) {
        E = E->IgnoreParenImpCasts();
        if (const auto *M = dyn_cast<MaterializeSequenceExpr>(E))
          E = M->getWrappedExpr();
        else if (const auto *B = dyn_cast<BoundsCheckExpr>(E))
          E = B->getGuardedExpr();
        else if (const auto *C = dyn_cast<ExprWithCleanups>(E))
          E = C->getSubExpr();
        else
          break;
      }
      return E;
    }
    static const MemberExpr *tagWrite(const Stmt *St, const BinaryOperator *&BO) {
      BO = dyn_cast_or_null<BinaryOperator>(bare(St));
      if (!BO || BO->getOpcode() != BO_Assign)
        return nullptr;
      const auto *ME = dyn_cast<MemberExpr>(BO->getLHS()->IgnoreParens());
      const auto *F = ME ? dyn_cast<FieldDecl>(ME->getMemberDecl()) : nullptr;
      return F && isTagField(F) ? ME : nullptr;
    }
    // The union tagged by F, when some member holds pointers.
    static const RecordDecl *unionOf(const FieldDecl *F) {
      for (const FieldDecl *U : F->getParent()->fields())
        if (taggedByName(U->getType()) == F->getName())
          return U->getType()->getAsRecordDecl();
      return nullptr;
    }
    bool matches(const MemberExpr *Tag, const BinaryOperator *BO,
                 const Stmt *Next) {
      const auto *TF = cast<FieldDecl>(Tag->getMemberDecl());
      const RecordDecl *U = unionOf(TF);
      if (!U)
        return true;
      bool Pointers = false;
      for (const FieldDecl *M : U->fields())
        Pointers |= !S->isSafeCPlainData(S->Context, M->getType(), true);
      if (!Pointers)
        return true;
      auto V = BO->getRHS()->getIntegerConstantExpr(S->Context);
      if (!V)
        return false;
      llvm::SmallPtrSet<const FieldDecl *, 4> Live;
      whenValues(S->Context, U,
                 [&](const FieldDecl *M, llvm::APSInt W, SourceLocation) {
                   if (llvm::APSInt::isSameValue(W, *V))
                     Live.insert(M);
                 });
      if (Live.empty())
        return true; // a value with no member: nothing can be read
      const auto *W = dyn_cast_or_null<BinaryOperator>(bare(Next));
      if (!W || W->getOpcode() != BO_Assign)
        return false;
      const auto *ME = dyn_cast<MemberExpr>(W->getLHS()->IgnoreParens());
      if (!ME || !Live.contains(dyn_cast<FieldDecl>(ME->getMemberDecl())))
        return false;
      // The same object: `b.k = ..; b.u.p = ..` or through an anonymous union.
      const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
      if (const auto *UM = dyn_cast<MemberExpr>(Base);
          UM && UM->getType()->getAsRecordDecl() == U) {
        Base = UM->getBase()->IgnoreParenImpCasts();
      }
      std::string A, B;
      llvm::raw_string_ostream OA(A), OB(B);
      Base->printPretty(OA, nullptr, S->getPrintingPolicy());
      Tag->getBase()->IgnoreParenImpCasts()->printPretty(
          OB, nullptr, S->getPrintingPolicy());
      if (A != B || Base->HasSideEffects(S->Context))
        return false;
      // Nothing may look at the object between the two.
      struct Calls : RecursiveASTVisitor<Calls> {
        bool Any = false;
        bool VisitCallExpr(CallExpr *C) {
          for (const Expr *Arg : C->arguments())
            Any |= Arg->getType()->isAnyPointerType() ||
                   Arg->getType()->isRecordType();
          return true;
        }
      } Cs;
      Cs.TraverseStmt(const_cast<Expr *>(W->getRHS()));
      const VarDecl *Root = safec::rootVar(Base);
      if (!Cs.Any)
        return true;
      // (A local the call is not given a pointer to is out of its reach.)
      struct Names : RecursiveASTVisitor<Names> {
        const VarDecl *VD;
        bool Any = false;
        bool VisitDeclRefExpr(DeclRefExpr *D) {
          Any |= D->getDecl() == VD;
          return true;
        }
      } Ns;
      Ns.VD = Root;
      Ns.TraverseStmt(const_cast<Expr *>(W->getRHS()));
      return Root && Root->isLocalVarDecl() && Root->getType()->isRecordType() &&
             !Ns.Any;
    }
    bool VisitCompoundStmt(CompoundStmt *CS) {
      for (auto I = CS->body_begin(), E = CS->body_end(); I != E; ++I) {
        const BinaryOperator *BO;
        if (const MemberExpr *T = tagWrite(*I, BO))
          if (matches(T, BO, std::next(I) == E ? nullptr : *std::next(I)))
            Paired.insert(BO);
      }
      return true;
    }
    bool VisitBinaryOperator(BinaryOperator *BO) {
      const BinaryOperator *Same;
      const MemberExpr *T = tagWrite(BO, Same);
      if (T && Same == BO && !Paired.contains(BO) && !matches(T, BO, nullptr))
        S->SafeCDiag(BO->getOperatorLoc(), "tag-unpaired") << T->getMemberDecl();
      return true;
    }
    // Initializers are checked whether or not a member is named later.
    bool VisitDeclStmt(DeclStmt *DS) {
      checkTagInits(*S, DS);
      return true;
    }
    bool VisitMemberExpr(MemberExpr *ME) {
      if (const auto *F = dyn_cast<FieldDecl>(ME->getMemberDecl()))
        if (const RecordDecl *P = F->getParent(); P && P->isUnion() &&
            getSafeCAnnotation(P, "safec_tagged_by"))
          Any = true;
      return true;
    }
  } Find;
  Find.S = &S;
  Find.TraverseStmt(Body);
  if (!Find.Any)
    return;

  // Locals whose address escapes may change behind a name the analysis sees.
  struct AddrFinder : RecursiveASTVisitor<AddrFinder> {
    llvm::DenseSet<const VarDecl *> Taken;
    void note(const Expr *E) {
      while (true) {
        E = E->IgnoreParenImpCasts();
        if (const auto *ME = dyn_cast<MemberExpr>(E); ME && !ME->isArrow())
          E = ME->getBase();
        else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E))
          E = ASE->getBase();
        else
          break;
      }
      if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
          Taken.insert(VD);
    }
    bool VisitUnaryOperator(UnaryOperator *UO) {
      if (UO->getOpcode() == UO_AddrOf)
        note(UO->getSubExpr());
      return true;
    }
    bool VisitImplicitCastExpr(ImplicitCastExpr *ICE) {
      if (ICE->getCastKind() == CK_ArrayToPointerDecay)
        note(ICE->getSubExpr());
      return true;
    }
  } Addr;
  Addr.TraverseStmt(Body);

  // Members written (`x.m = v`): a write needs the tag set, and makes the
  // member written; a read needs both.
  struct LHSFinder : RecursiveASTVisitor<LHSFinder> {
    llvm::DenseSet<const MemberExpr *> Writes;
    bool VisitBinaryOperator(BinaryOperator *BO) {
      if (BO->getOpcode() == BO_Assign)
        if (const auto *ME = dyn_cast<MemberExpr>(BO->getLHS()->IgnoreParens()))
          Writes.insert(ME);
      return true;
    }
  } LHS;
  LHS.TraverseStmt(Body);

  CFG::BuildOptions Opts;
  Opts.setAllAlwaysAdd(); // every member access is its own element
  std::unique_ptr<CFG> G = CFG::buildCFG(Fn, Body, &S.Context, Opts);
  if (!G)
    return;
  ASTContext &Ctx = S.Context;
  // In[b]: facts at block entry; Done marks blocks with a computed In.
  llvm::DenseMap<const CFGBlock *, TagFacts> In;
  llvm::DenseSet<const CFGBlock *> Done;
  llvm::SmallVector<const CFGBlock *, 16> Work;
  Done.insert(&G->getEntry());
  Work.push_back(&G->getEntry());
  auto Run = [&](const CFGBlock &B, TagFacts F, bool Report) {
    for (const CFGElement &E : B) {
      std::optional<CFGStmt> CS = E.getAs<CFGStmt>();
      if (!CS)
        continue;
      const Stmt *St = CS->getStmt();
      if (Report)
        if (const auto *DS = dyn_cast<DeclStmt>(St))
          (void)DS;
      {
        if (const auto *ME = dyn_cast<MemberExpr>(St)) {
          const auto *M = dyn_cast<FieldDecl>(ME->getMemberDecl());
          const RecordDecl *U = M ? M->getParent() : nullptr;
          StringRef Tag = taggedByName(U);
          // The union is reached through its field in the tagged struct.
          const auto *UnionME =
              Tag.empty() ? nullptr
                          : dyn_cast<MemberExpr>(ME->getBase()->IgnoreParenImpCasts());
          bool IsWrite = LHS.Writes.count(ME);
          std::optional<TagPath> P =
              UnionME ? structPath(UnionME->getBase()) : std::nullopt;
          if (UnionME && !P && Report)
            // Reached through something the analysis cannot follow (an
            // array element, a call result): it cannot know the tag.
            S.SafeCDiag(ME->getMemberLoc(), "union-member-untracked") << M;
          if (UnionME && P) {
              const FieldDecl *TagF = nullptr;
              const RecordDecl *Outer =
                  cast<FieldDecl>(UnionME->getMemberDecl())->getParent();
              for (const FieldDecl *Sib : Outer->fields())
                if (Sib->getName() == Tag)
                  TagF = Sib;
              if (TagF) {
                P->Fields.push_back(TagF);
                const TagValues *Known = lookupFact(F, *P);
                // Fine when every value the tag may hold names this member.
                TagValues Mine;
                std::string Want;
                whenValues(Ctx, U, [&](const FieldDecl *Mem, llvm::APSInt V,
                                       SourceLocation) {
                  if (Mem != M)
                    return;
                  Mine.push_back(V);
                  if (Want.empty()) {
                    Want = TagF->getType()->isBooleanType()
                               ? (V == 0 ? "false" : "true")
                               : toString(V, 10);
                    if (const auto *ET = TagF->getType()->getAs<EnumType>())
                      for (const EnumConstantDecl *EC :
                           ET->getOriginalDecl()->getDefinitionOrSelf()->enumerators())
                        if (llvm::APSInt::isSameValue(EC->getInitVal(), V))
                          Want = EC->getName().str();
                  }
                });
                bool Ok = Known && !Known->empty() &&
                          llvm::all_of(*Known, [&](const llvm::APSInt &V) {
                  return hasValue(Mine, V);
                });
                if (Ok && IsWrite)
                  for (auto &KV : F)
                    if (KV.first == *P)
                      KV.second.Written = true;
                bool Unwritten = Ok && !IsWrite && !Known->Written;
                if (Unwritten && Report)
                  S.SafeCDiag(ME->getMemberLoc(), "union-member-unwritten")
                      << M << Tag;
                if (!Ok && !Want.empty()) {
                  unsigned Hint = !TagF->getType()->isBooleanType() ? 0
                                  : Want == "true"                   ? 1
                                                                     : 2;
                  if (Report)
                    S.SafeCDiag(ME->getMemberLoc(), "union-member-unchecked")
                        << (IsWrite ? 1 : 0) << M << Tag << Want << Hint;
                }
              }
            }
        }
      }
      transfer(Ctx, St, F, Addr.Taken);
    }
    return F;
  };
  // Forward dataflow to a fixed point, then one reporting pass.
  unsigned Steps = 0;
  while (!Work.empty() && ++Steps < 10000) {
    const CFGBlock *B = Work.pop_back_val();
    TagFacts Out = Run(*B, In[B], /*Report=*/false);
    unsigned I = 0;
    for (const CFGBlock::AdjacentBlock &A : B->succs()) {
      const CFGBlock *Succ = A.getReachableBlock();
      unsigned Idx = I++;
      if (!Succ)
        continue;
      TagFacts EF = edgeFacts(Ctx, *B, Idx, Succ, Out);
      if (Done.insert(Succ).second) {
        In[Succ] = EF;
        Work.push_back(Succ);
        continue;
      }
      TagFacts M = meet(In[Succ], EF);
      if (!sameFacts(M, In[Succ])) {
        In[Succ] = M;
        Work.push_back(Succ);
      }
    }
  }
  for (const CFGBlock *B : *G)
    if (Done.count(B))
      Run(*B, In[B], /*Report=*/true);
}

/// The kind of type in \p T (at any depth) that Safe C has no rules for, or
/// null.
static const char *unknownType(QualType T, unsigned Depth = 0) {
  if (T.isNull() || Depth > 16)
    return nullptr;
  const Type *C = T.getCanonicalType().getTypePtr();
  switch (C->getTypeClass()) {
  case Type::Builtin: {
    const auto *B = cast<BuiltinType>(C);
    // (Scalable vectors, OpenCL and other targets' types are sizeless or odd.)
    return B->isInteger() || B->isFloatingPoint() || B->isVoidType() ||
                   B->getKind() == BuiltinType::NullPtr || B->isPlaceholderType()
               ? nullptr
               : "Builtin";
  }
  case Type::Record:
  case Type::Enum:
  case Type::BitInt:
  case Type::Vector:
  case Type::ExtVector:
  case Type::FunctionNoProto: // a call through one is an error
    return nullptr;
  case Type::Complex:
    return nullptr;
  case Type::Pointer:
    return unknownType(cast<PointerType>(C)->getPointeeType(), Depth + 1);
  case Type::Atomic:
    return unknownType(cast<AtomicType>(C)->getValueType(), Depth + 1);
  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
    return unknownType(cast<ArrayType>(C)->getElementType(), Depth + 1);
  case Type::FunctionProto: {
    const auto *P = cast<FunctionProtoType>(C);
    if (const char *K = unknownType(P->getReturnType(), Depth + 1))
      return K;
    for (QualType A : P->param_types())
      if (const char *K = unknownType(A, Depth + 1))
        return K;
    return nullptr;
  }
  default:
    return C->getTypeClassName();
  }
}

/// Kinds of declarations and types that Safe C has rules for (see VisitStmt
/// for statements); anything else is an error.
// Off only for lit tests, which have no system headers to include (the driver
// refuses it).
static llvm::cl::opt<bool> SafeCRedeclared(
    "safe-c-redeclared",
    llvm::cl::desc("Safe C: reject hand-written declarations of library "
                   "functions that access memory"),
    llvm::cl::init(true));

static void checkDeclAllowed(Sema &S, const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D);
      FD && SafeCRedeclared && FD->getIdentifier() && FD->isExternC() &&
      !S.getSourceManager().isInSystemHeader(FD->getLocation())) {
    // The header's declaration says how much these access; one written by hand
    // says whatever it likes.
    static const llvm::StringSet<> Memory = {
        "memcpy", "memmove", "memset", "memcmp", "memchr", "bzero", "bcopy",
        "strlen", "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp",
        "strchr", "strrchr", "strstr", "strlcpy", "strlcat"};
    auto Bounded = [](QualType T) {
      return T->isBoundsAttributedType() || T->isPointerTypeWithBounds();
    };
    bool Marked = Bounded(FD->getReturnType()) ||
                  llvm::any_of(FD->parameters(), [&](const ParmVarDecl *P) {
                    return Bounded(P->getType());
                  });
    // safec.h's own annotated redeclarations (for C libraries that do not
    // annotate themselves, glibc and musl) are not a user's. The driver
    // includes it by full path, so it is not found as a system header.
    auto IsSafeCHeader = [&] {
      llvm::SmallString<128> H(
          S.getPreprocessor().getHeaderSearchInfo().getHeaderSearchOpts()
              .ResourceDir);
      llvm::sys::path::append(H, "include", "safec.h");
      return S.getSourceManager().getFilename(
                 S.getSourceManager().getExpansionLoc(FD->getLocation())) ==
             H.str();
    };
    if (!FD->isThisDeclarationADefinition() && FD->getBuiltinID() &&
        (Marked || Memory.contains(FD->getName())) && !IsSafeCHeader())
      S.SafeCDiag(FD->getLocation(), "redeclared") << FD;
  }
  if (D->isImplicit() ||
      S.getSourceManager().isInSystemHeader(D->getLocation()))
    return;
  switch (D->getKind()) {
  case Decl::TranslationUnit:
  case Decl::Function:
  case Decl::Var:
  case Decl::ParmVar:
  case Decl::ImplicitParam:
  case Decl::Field:
  case Decl::IndirectField:
  case Decl::Record:
  case Decl::Enum:
  case Decl::EnumConstant:
  case Decl::Typedef:
  case Decl::Label:
  case Decl::Empty:
  case Decl::StaticAssert:
  case Decl::PragmaComment:
  case Decl::PragmaDetectMismatch:
  case Decl::FileScopeAsm: // rejected with its own message
  case Decl::Block:
    break;
  default:
    S.SafeCDiag(D->getLocation(), "not-in-safe-c") << D->getDeclKindName();
    return;
  }
  if (const auto *V = dyn_cast<ValueDecl>(D); V && !isa<LabelDecl>(D))
    if (const char *K = unknownType(V->getType()))
      S.SafeCDiag(D->getLocation(), "type-not-in-safe-c") << K;
}

void clang::checkSafeCDeclarations(Sema &S) {
  struct Kinds : RecursiveASTVisitor<Kinds> {
    Sema &S;
    explicit Kinds(Sema &S) : S(S) {}
    bool VisitDecl(Decl *D) {
      checkDeclAllowed(S, D);
      return true;
    }
  } K(S);
  K.TraverseDecl(S.Context.getTranslationUnitDecl());
}
