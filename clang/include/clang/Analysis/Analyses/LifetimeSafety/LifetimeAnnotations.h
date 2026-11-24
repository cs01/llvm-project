//===- LifetimeAnnotations.h -  -*--------------- C++--------------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Helper functions to inspect and infer lifetime annotations.
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_LIFETIMEANNOTATIONS_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_LIFETIMEANNOTATIONS_H

#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

namespace clang {
class LifetimeBoundAttr;
} // namespace clang

namespace clang ::lifetimes {

// This function is needed because Decl::isInStdNamespace will return false for
// iterators in some STL implementations due to them being defined in a
// namespace outside of the std namespace.
bool isInStlNamespace(const Decl *D);

bool isPointerLikeType(QualType QT);

/// Safe C: the kind of a pointer type, from its [[clang::annotate_type]]
/// sugar ("owned", "excl", "static", "unchecked"), or "" for a plain borrow.
/// Arrays give their elements' kind.
llvm::StringRef safeCPointerKind(QualType T);
/// What a pointer type promises beyond its C type (ownership kind, bounds).
std::string safeCPromise(QualType T);
/// Everything other files are told about a function or global beyond its C
/// type: the promises of its types, its attributes, and the contents of
/// structs it holds or passes by value.
std::string safeCSignature(const ValueDecl *VD);
/// The structs an interface points to whose contents this file sees, as
/// name and contents. (A file that sees only `struct s;` has none to compare.)
std::vector<std::pair<std::string, std::string>>
safeCPointedStructs(const ValueDecl *VD);
/// Whether a call may read or change this program's globals (rule 14).
bool safeCMayTouchGlobals(const CallExpr *CE, const SourceManager &SM);
bool safeCFunctionTouchesGlobals(const FunctionDecl *FD,
                                 const SourceManager &SM);
/// Whether a value of type T holds an owned pointer (opaque structs: assumed).
bool safeCReachesOwner(QualType T, unsigned Depth = 0);
/// The variable a borrow local is rooted at (`q` from `q = x->p` is x),
/// following its values in Body; VD itself when it has no single root.
const VarDecl *safeCResolveRoot(const VarDecl *VD, const Stmt *Body,
                                unsigned Depth);
/// The kind of a parameter of type \p T: its written kind, or (with
/// -mllvm -safe-c-excl-default) "excl" for a plain pointer to non-const data.
llvm::StringRef safeCParamKind(QualType T);
/// safeCParamKind for a parameter declared outside system headers.
llvm::StringRef safeCParamDeclKind(const ParmVarDecl *P);
/// safeCParamDeclKind for parameters, safeCPointerKind for other declarations.
llvm::StringRef safeCDeclKind(const ValueDecl *D);

/// Safe C: whether the M2 ownership rules are on (default; turned off by
/// -mllvm -safe-c-ownership=false).
bool safeCOwnershipFlag();

/// Safe C: the local variable or parameter a place is reached from (`l` in
/// `l->head->next`, `v` in `v.items[i]`), or null for globals and others.
const VarDecl *safeCRootVar(const Expr *E);

/// Safe C: E without parentheses, implicit casts, or the opaque values bounds
/// safety wraps call arguments in.
const Expr *safeCPeel(const Expr *E);

/// Safe C: a struct marked _View (it holds borrows, and lives only in locals
/// and parameters).
bool isSafeCViewType(QualType T);

/// Returns the most recent declaration of the method to ensure all
/// lifetime-bound attributes from redeclarations are considered.
const FunctionDecl *getDeclWithMergedLifetimeBoundAttrs(const FunctionDecl *FD);

/// Returns the most recent declaration of the method to ensure all
/// lifetime-bound attributes from redeclarations are considered.
const CXXMethodDecl *
getDeclWithMergedLifetimeBoundAttrs(const CXXMethodDecl *CMD);

// Return true if this is an "normal" assignment operator.
// We assume that a normal assignment operator always returns *this, that is,
// an lvalue reference that is the same type as the implicit object parameter
// (or the LHS for a non-member operator==).
bool isNormalAssignmentOperator(const FunctionDecl *FD);

/// Returns true if this is an assignment operator where the parameter
/// has the lifetimebound attribute.
bool isAssignmentOperatorLifetimeBound(const CXXMethodDecl *CMD);

/// Returns the lifetimebound attribute for the implicit this parameter, if it
/// exists on the current type.
const LifetimeBoundAttr *
getDirectImplicitObjectLifetimeBoundAttr(const FunctionDecl *FD);

/// Returns the lifetimebound attribute for the implicit this parameter, if it
/// exists on any redeclaration.
const LifetimeBoundAttr *
getImplicitObjectParamLifetimeBoundAttr(const FunctionDecl *FD);

/// Returns true if the implicit object parameter (this) should be considered
/// lifetimebound, either due to an explicit lifetimebound attribute on the
/// method or because it's a normal assignment operator.
bool implicitObjectParamIsLifetimeBound(const FunctionDecl *FD);

/// Returns true if \p PVD should be considered lifetimebound, either due to an
/// explicit lifetimebound attribute or because it is a constructor parameter
/// marked `lifetime_capture_by(this)`, which has the same meaning.
bool paramIsLifetimeBound(const ParmVarDecl &PVD);

/// Check if a function has a lifetime_capture_by attribute on its declaration
/// or its function type (which represents the implicit 'this' parameter for
/// methods). Returns the attribute if found, nullptr otherwise.
const LifetimeCaptureByAttr *
getCaptureByAttrFromFunctionType(const FunctionDecl *FD);

using LifetimeBoundParamInfo =
    llvm::PointerUnion<const ParmVarDecl *, const CXXMethodDecl *>;

/// Stores the callee and normalized arguments for a function call.
struct FunctionCallInfo {
  const FunctionDecl *FD = nullptr;
  llvm::SmallVector<const Expr *, 4> Args;

  explicit FunctionCallInfo(const Expr *Call);
};

/// Returns true if argument 0 of a call to \p FD (as listed by
/// FunctionCallInfo) is the object argument, bound either to the implicit
/// object parameter or to an explicit object parameter. Constructors have no
/// object argument.
bool hasObjectArg(const FunctionDecl &FD);

/// Returns true if argument 0 of a call to \p FD is the implicit object
/// argument. An explicit object parameter is an ordinary parameter.
bool hasImplicitObjectArg(const FunctionDecl &FD);

/// Returns the parameter that argument \p I of a call to \p FD binds to, or
/// null for the implicit object argument and for variadic arguments.
const ParmVarDecl *getParamForArg(const FunctionDecl &FD, unsigned I);

/// Returns the parameter corresponding to argument I when the argument should
/// be tracked for lifetime safety.
std::optional<LifetimeBoundParamInfo>
getTrackedArgInfo(const FunctionDecl *FD, llvm::ArrayRef<const Expr *> Args,
                  unsigned I);

/// Returns lifetime safety tracking info for the call argument containing
/// Source.
std::optional<LifetimeBoundParamInfo>
getTrackingInfoForCallArg(const Expr *Call, const Expr *Source);

// Returns true if the implicit object argument (this) of a method call should
// be tracked for GSL lifetime analysis. This applies to STL methods that return
// pointers or references that depend on the lifetime of the object, such as
// container iterators (begin, end), data accessors (c_str, data, get),
// element accessors (operator[], operator*, front, back, at), or propagating
// operations (operator+, operator-, operator++, operator--).
bool shouldTrackImplicitObjectArg(const Expr &ImplicitObjectArgument,
                                  const CXXMethodDecl *Callee,
                                  bool RunningUnderLifetimeSafety);

// Returns true if the first argument of a free function should be tracked for
// GSL lifetime analysis. This applies to STL free functions that take a pointer
// to a GSL Owner or Pointer and return a pointer or reference that depends on
// the lifetime of the argument, such as std::begin, std::data, std::get, or
// std::any_cast.
bool shouldTrackFirstArgument(const FunctionDecl *FD);

// Returns true if the second argument of a free function should be tracked for
// lifetime analysis. This applies to free operator functions that take a
// GSL Pointer as their second argument.
bool shouldTrackSecondArgument(const FunctionDecl *FD);

// Tells whether the type is annotated with [[gsl::Pointer]].
bool isGslPointerType(QualType QT);
// Tells whether the type is annotated with [[gsl::Owner]].
bool isGslOwnerType(QualType QT);
bool isGslOwnerType(const CXXRecordDecl *RD);

// Tells whether the given constructor belongs to an Owner type and the
// parameter is of pointer type. This is useful to disable inference on owning
// pointers being captured by owners.
bool isOwnerPtrCtor(const CXXConstructorDecl *Ctor, const ParmVarDecl *PVD);

// Returns true if the given method is std::unique_ptr::release().
// This is treated as a move in lifetime analysis to avoid false-positives
// when ownership is manually transferred.
bool isUniquePtrRelease(const CXXMethodDecl &MD);

// Returns true if the given method invalidates references tracked by lifetime
// analysis (e.g. vector::push_back). Methods that only invalidate iterators but
// not references (e.g. unordered_map::emplace) are not considered invalidating
// here.
//
// Container invalidation rules are based on:
// https://en.cppreference.com/w/cpp/container#Iterator_invalidation
bool isInvalidationMethod(const CXXMethodDecl &MD);

// Returns true if the function destroys its first argument
// (e.g., destructors via implicit 'this', std::destroy_at).
bool destructsFirstArg(const FunctionDecl &FD);

/// Returns true if the function allocates memory that must be freed by the
/// caller (e.g. functions annotated with `ownership_returns`, such as
/// `__attribute__((ownership_returns(malloc)))`).
bool isAllocatingFunction(const FunctionDecl &FD);

/// Returns true if the function frees memory it takes ownership of (e.g.
/// functions annotated with `ownership_takes`, such as
/// `__attribute__((ownership_takes(malloc, 1)))`).
bool isFreeingFunction(const FunctionDecl &FD);

/// Returns true for standard library callable wrappers (e.g., std::function)
/// that can propagate the stored lambda's origins.
bool isStdCallableWrapperType(const CXXRecordDecl *RD);

/// Returns true for std reference-cast builtins (e.g., std::move). Their result
/// refers to the same object as the argument, so all origins propagate from
/// argument to result.
bool isStdReferenceCast(const FunctionDecl *FD);

/// Skips parentheses and implicit casts. Unlike IgnoreParenImpCasts(), stops at
/// a MaterializeTemporaryExpr, which owns the storage of a temporary.
const Expr *ignoreParensAndImpCasts(const Expr *E);

} // namespace clang::lifetimes

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_LIFETIMEANNOTATIONS_H
