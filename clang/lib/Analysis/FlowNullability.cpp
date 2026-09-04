//===- FlowNullability.cpp - Flow-sensitive null dereference checking -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a CFG-based forward dataflow analysis that detects
// dereferences of nullable pointers, tracking nullability narrowing through
// control flow (null checks, early returns, assertions, etc.).
//
// Design overview
//
// The lattice is NullState. Its proof sets, NarrowedVars and NarrowedMembers,
// hold pointers shown non-null and intersect at joins: a pointer stays
// narrowed only if every incoming path agrees. Its taint sets, NullableVars
// and NullableMembers, hold pointers the flow has shown may be null (assigned
// nullptr, reset, moved-from) and union at joins. The guard and alias maps
// (BoolGuards, Aliases, MemberAliases, AddrOfTargets) intersect with value
// equality, so a fact survives a join only when both sides recorded exactly
// the same one.
//
// States are kept per CFG edge, keyed by (predecessor, successor) block ids,
// and a block's entry state is the join of its incoming edge states. This is
// what lets the two branches of a condition receive different states: the
// edge that proves p non-null carries p narrowed, the other does not.
//
// analyzeCondition turns a branch condition into ConditionResult facts of the
// form "pointer X is non-null when the condition is true (or false, when
// Negated)". narrowOnTerminator runs it on a block's terminator and applies
// each fact to the true-edge or false-edge state as its Negated flag says.
// The same machinery serves guard flags (bool ok = p != nullptr; if (ok)),
// __builtin_assume, and ternary arms.
//
// Calls deliberately do not invalidate narrowing, even when a pointer's
// address escapes: doing so floods common output-parameter idioms with false
// positives. Clang's ThreadSafety analysis makes the same trade-off.
//
// The worklist re-processes a block only when its entry state changes. The
// lattice is not provably monotone (facts are both inserted and erased), so
// runFlowNullabilityAnalysis caps total block visits as a termination safety
// net and suppresses summary inference if the cap fires.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/FlowNullability.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/DataflowWorklist.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <optional>
#include <utility>

#define DEBUG_TYPE "flow-nullability"

STATISTIC(NumFunctionsAnalyzed, "Number of functions analyzed");
STATISTIC(NumBlocksProcessed, "Number of CFG blocks processed");
STATISTIC(NumFixpointBailouts,
          "Number of analyses stopped by the block-visit safety cap");
STATISTIC(NumDereferenceWarnings, "Number of nullable dereference warnings");
STATISTIC(NumArithmeticWarnings, "Number of nullable arithmetic warnings");
STATISTIC(NumReturnWarnings, "Number of nullable return warnings");
STATISTIC(NumAssignmentWarnings, "Number of nullable assignment warnings");
STATISTIC(NumArgumentWarnings, "Number of nullable argument warnings");

using namespace clang;

FlowNullabilityHandler::~FlowNullabilityHandler() = default;

//===----------------------------------------------------------------------===//
// Access paths and AST helpers
//===----------------------------------------------------------------------===//

namespace {

/// Access path from a root variable through a chain of field accesses.
/// Represents expressions like: var.field, var->inner->field, this->a.b.
/// Root is nullptr for this-> access paths.
struct MemberAccessPath {
  const VarDecl *Root = nullptr;
  llvm::SmallVector<const FieldDecl *, 2> Fields;

  const FieldDecl *leafField() const {
    assert(!Fields.empty() && "leafField() on empty path");
    return Fields.back();
  }

  bool operator==(const MemberAccessPath &O) const {
    return Root == O.Root && Fields == O.Fields;
  }
  // DenseMap::operator== on MemberAliases compares values with !=.
  bool operator!=(const MemberAccessPath &O) const { return !(*this == O); }
};

} // end anonymous namespace

template <> struct llvm::DenseMapInfo<MemberAccessPath> {
  static MemberAccessPath getEmptyKey() {
    return {DenseMapInfo<const VarDecl *>::getEmptyKey(), {}};
  }
  static MemberAccessPath getTombstoneKey() {
    return {DenseMapInfo<const VarDecl *>::getTombstoneKey(), {}};
  }
  static unsigned getHashValue(const MemberAccessPath &P) {
    unsigned H = DenseMapInfo<const VarDecl *>::getHashValue(P.Root);
    for (const auto *FD : P.Fields)
      H = llvm::hash_combine(H,
                             DenseMapInfo<const FieldDecl *>::getHashValue(FD));
    return H;
  }
  static bool isEqual(const MemberAccessPath &L, const MemberAccessPath &R) {
    return L == R;
  }
};

/// Walk a MemberExpr chain to its root, collecting FieldDecls along the way.
/// Returns nullopt if the root is not a VarDecl (via DeclRefExpr) or
/// CXXThisExpr. Root is nullptr for this-> access paths.
static std::optional<MemberAccessPath> decomposeMemberAccess(const Expr *E) {
  llvm::SmallVector<const FieldDecl *, 2> Fields;
  E = E->IgnoreParenImpCasts();

  while (const auto *ME = dyn_cast<MemberExpr>(E)) {
    if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl()))
      Fields.push_back(FD);
    else
      return std::nullopt;
    E = ME->getBase()->IgnoreParenImpCasts();
  }

  if (Fields.empty())
    return std::nullopt;

  std::reverse(Fields.begin(), Fields.end());

  MemberAccessPath Path;
  Path.Fields = std::move(Fields);

  if (isa<CXXThisExpr>(E)) {
    Path.Root = nullptr;
  } else if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      Path.Root = VD;
    else
      return std::nullopt;
  } else {
    return std::nullopt;
  }

  return Path;
}

/// Whether Path starts with Prefix: same root, and Prefix's fields lead
/// Path's.
static bool pathHasPrefix(const MemberAccessPath &Path,
                          const MemberAccessPath &Prefix) {
  return Path.Root == Prefix.Root &&
         Path.Fields.size() >= Prefix.Fields.size() &&
         std::equal(Prefix.Fields.begin(), Prefix.Fields.end(),
                    Path.Fields.begin());
}

//===----------------------------------------------------------------------===//
// Lattice
//===----------------------------------------------------------------------===//

namespace {

/// A tracked pointer: a local variable or parameter, or a member access path
/// (exactly one of VD and Path is set).
struct PtrRef {
  const VarDecl *VD = nullptr;
  std::optional<MemberAccessPath> Path;

  /// Resolve E (parens and implicit casts ignored) to a tracked pointer, or
  /// nullopt when it is neither a DeclRefExpr to a VarDecl nor a member
  /// access chain rooted at a VarDecl or this.
  static std::optional<PtrRef> fromExpr(const Expr *E) {
    E = E->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        return PtrRef{VD, std::nullopt};
      return std::nullopt;
    }
    if (auto P = decomposeMemberAccess(E))
      return PtrRef{nullptr, std::move(P)};
    return std::nullopt;
  }
  /// Declared type of the variable or the leaf field.
  QualType getType() const {
    return VD ? VD->getType() : Path->leafField()->getType();
  }
  bool operator==(const PtrRef &O) const {
    return VD == O.VD && Path == O.Path;
  }
};

/// One fact extracted from a branch condition (or a guard variable's
/// initializer): the pointer Ref is non-null when the condition is true
/// (Negated=false) or when it is false (Negated=true).
struct ConditionResult {
  PtrRef Ref;
  bool Negated = false;

  bool operator==(const ConditionResult &O) const {
    return Ref == O.Ref && Negated == O.Negated;
  }
};

/// Per-block dataflow lattice tracking which pointers are narrowed (known
/// non-null) or nullable.
struct NullState {
  /// Pointers proven non-null by control flow (null checks, nonnull init,
  /// etc.). A variable should not be in both NarrowedVars and NullableVars:
  /// narrowing is always erased before re-evaluating nullability on
  /// reassignment.
  llvm::DenseSet<const VarDecl *> NarrowedVars;
  /// Unified member narrowing: covers both this->field and var.field paths,
  /// including nested access like var.inner.field (arbitrary depth).
  llvm::DenseSet<MemberAccessPath> NarrowedMembers;
  llvm::DenseSet<const VarDecl *> NullableVars;
  /// Member access paths known to be nullable at runtime (e.g., smart pointer
  /// members after reset() or std::move()). Parallels NarrowedMembers.
  llvm::DenseSet<MemberAccessPath> NullableMembers;

  /// Maps integer-typed guard variables (bool or int flags) to the null-check
  /// facts they capture, e.g. bool valid = (p != nullptr) stores
  /// {valid -> [(p, Negated=false)]}. Each fact reads exactly like a branch
  /// condition result: when the guard is true the Negated=false facts hold,
  /// when it is false the Negated=true facts hold. A guard built from p && q
  /// carries one fact per conjunct.
  using BoolGuardMap =
      llvm::DenseMap<const VarDecl *, llvm::SmallVector<ConditionResult, 2>>;
  BoolGuardMap BoolGuards;

  /// Simple pointer alias tracking: y = x stores {y -> x}, meaning y holds
  /// the same pointer value as x. When either is narrowed by a branch
  /// condition, the other is narrowed too (at the edge-state level).
  /// Depth-1 only: if z = y and y -> x, we store z -> x (canonical target).
  using AliasMap = llvm::DenseMap<const VarDecl *, const VarDecl *>;
  AliasMap Aliases;

  /// Local pointer copied from a member path: T *q = s->next stores
  /// {q -> s.next}. Narrowing either side narrows the other, so
  /// if (s->next) { T *q = s->next; *q; } and T *q = s->next; if (q)
  /// *s->next both work. Dropped when q is reassigned or the path is
  /// invalidated.
  using MemberAliasMap = llvm::DenseMap<const VarDecl *, MemberAccessPath>;
  MemberAliasMap MemberAliases;

  /// Tracks "pp holds &local": T** pp = &p records pp -> p. Used to
  /// invalidate p's narrowing on *pp = anything, since a store through the
  /// pointer-to-pointer can change p. Entries are dropped when pp is
  /// reassigned.
  using AddrOfTargetMap = llvm::DenseMap<const VarDecl *, const VarDecl *>;
  AddrOfTargetMap AddrOfTargets;

  void markNarrowed(const VarDecl *VD) {
    NarrowedVars.insert(VD);
    NullableVars.erase(VD);
  }
  void markNullable(const VarDecl *VD) {
    NarrowedVars.erase(VD);
    NullableVars.insert(VD);
  }
  void markNarrowed(const MemberAccessPath &P) {
    NarrowedMembers.insert(P);
    NullableMembers.erase(P);
  }
  void markNullable(const MemberAccessPath &P) {
    NarrowedMembers.erase(P);
    NullableMembers.insert(P);
  }

  bool isNarrowed(const PtrRef &R) const {
    return R.VD ? NarrowedVars.contains(R.VD)
                : NarrowedMembers.contains(*R.Path);
  }
  bool isNullable(const PtrRef &R) const {
    return R.VD ? NullableVars.contains(R.VD)
                : NullableMembers.contains(*R.Path);
  }
  void markNarrowed(const PtrRef &R) {
    if (R.VD)
      markNarrowed(R.VD);
    else
      markNarrowed(*R.Path);
  }
  void markNullable(const PtrRef &R) {
    if (R.VD)
      markNullable(R.VD);
    else
      markNullable(*R.Path);
  }
  /// Forget both the narrowed and the nullable fact about R.
  void clear(const PtrRef &R) {
    if (R.VD) {
      NarrowedVars.erase(R.VD);
      NullableVars.erase(R.VD);
    } else {
      NarrowedMembers.erase(*R.Path);
      NullableMembers.erase(*R.Path);
    }
  }

  bool operator==(const NullState &Other) const {
    return NarrowedVars == Other.NarrowedVars &&
           NarrowedMembers == Other.NarrowedMembers &&
           NullableVars == Other.NullableVars &&
           NullableMembers == Other.NullableMembers &&
           BoolGuards == Other.BoolGuards && Aliases == Other.Aliases &&
           MemberAliases == Other.MemberAliases &&
           AddrOfTargets == Other.AddrOfTargets;
  }
  bool operator!=(const NullState &Other) const { return !(*this == Other); }
};

} // end anonymous namespace

static NullState join(const NullState &A, const NullState &B) {
  NullState Result;
  // Narrowed = intersection: only narrowed if ALL paths agree.
  for (const auto *VD : A.NarrowedVars)
    if (B.NarrowedVars.contains(VD))
      Result.NarrowedVars.insert(VD);
  for (const auto &MK : A.NarrowedMembers)
    if (B.NarrowedMembers.contains(MK))
      Result.NarrowedMembers.insert(MK);
  // Nullable = union: if nullable on either path, it's nullable.
  for (const auto *VD : A.NullableVars)
    Result.NullableVars.insert(VD);
  for (const auto *VD : B.NullableVars)
    Result.NullableVars.insert(VD);
  for (const auto &Path : A.NullableMembers)
    Result.NullableMembers.insert(Path);
  for (const auto &Path : B.NullableMembers)
    Result.NullableMembers.insert(Path);
  // Maps intersect with value equality: an entry survives only when both
  // sides map the key to the same fact.
  for (const auto &[BoolVD, GuardInfo] : A.BoolGuards) {
    auto It = B.BoolGuards.find(BoolVD);
    if (It != B.BoolGuards.end() && It->second == GuardInfo)
      Result.BoolGuards[BoolVD] = GuardInfo;
  }
  for (const auto &[AliasVD, TargetVD] : A.Aliases) {
    auto It = B.Aliases.find(AliasVD);
    if (It != B.Aliases.end() && It->second == TargetVD)
      Result.Aliases[AliasVD] = TargetVD;
  }
  for (const auto &[AliasVD, Path] : A.MemberAliases) {
    auto It = B.MemberAliases.find(AliasVD);
    if (It != B.MemberAliases.end() && It->second == Path)
      Result.MemberAliases[AliasVD] = Path;
  }
  for (const auto &[PtrPtrVD, TargetVD] : A.AddrOfTargets) {
    auto It = B.AddrOfTargets.find(PtrPtrVD);
    if (It != B.AddrOfTargets.end() && It->second == TargetVD)
      Result.AddrOfTargets[PtrPtrVD] = TargetVD;
  }
  // Narrowed wins over a stale nullable entry, so NullableVars does not
  // accumulate across fixpoint iterations.
  for (const auto *VD : Result.NarrowedVars)
    Result.NullableVars.erase(VD);
  for (const auto &Path : Result.NarrowedMembers)
    Result.NullableMembers.erase(Path);
  LLVM_DEBUG({
    llvm::dbgs() << "  join: narrowed=" << Result.NarrowedVars.size()
                 << " nullable=" << Result.NullableVars.size()
                 << " members=" << Result.NarrowedMembers.size()
                 << " aliases=" << Result.Aliases.size() << "\n";
  });
  return Result;
}

//===----------------------------------------------------------------------===//
// Expression unwrapping helpers
//===----------------------------------------------------------------------===//

/// Look through __builtin_expect(cond, ...) to cond.
static const Expr *unwrapBuiltinExpect(const Expr *E) {
  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    if (const auto *Callee = CE->getDirectCallee()) {
      unsigned BuiltinID = Callee->getBuiltinID();
      if ((BuiltinID == Builtin::BI__builtin_expect ||
           BuiltinID == Builtin::BI__builtin_expect_with_probability) &&
          CE->getNumArgs() >= 1) {
        // IgnoreParenCasts, not ImpCasts: __builtin_expect((long)(p == 0), 0)
        // wraps the condition in an explicit integer cast.
        return CE->getArg(0)->IgnoreParenCasts();
      }
    }
  }
  return E;
}

/// Look through an OpaqueValueExpr (the shared operand of a GNU a ?: b)
/// to the expression it stands for.
static const Expr *stripOpaqueValue(const Expr *E) {
  if (const auto *OVE = dyn_cast<OpaqueValueExpr>(E))
    if (const Expr *Src = OVE->getSourceExpr())
      return Src->IgnoreParenImpCasts();
  return E;
}

/// Constant truth value of an integer/bool expression, if it has one.
static std::optional<bool> constantTruth(const Expr *E, ASTContext &Ctx) {
  E = E->IgnoreParenImpCasts();
  if (const auto *BL = dyn_cast<CXXBoolLiteralExpr>(E))
    return BL->getValue();
  if (E->getType()->isIntegerType())
    if (auto V = E->getIntegerConstantExpr(Ctx))
      return !V->isZero();
  return std::nullopt;
}

/// Strip explicit casts to bool so the underlying condition is visible.
/// glibc/libstdc++ define assert(expr) as
///   (static_cast<bool>(expr) ? void(0) : __assert_fail(...))
/// and IgnoreParenImpCasts() leaves that CXXStaticCastExpr in place, hiding
/// the tested pointer from narrowing.
static const Expr *ignoreExplicitBoolCast(const Expr *E) {
  while (const auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
    if (!CE->getType()->isBooleanType())
      break;
    E = CE->getSubExpr()->IgnoreParenImpCasts();
  }
  return E;
}

/// Strip explicit casts whose source and result are both pointer types, so
/// *(int*)o still reaches the tracked VarDecl o (IgnoreParenImpCasts leaves
/// explicit casts in place). Integer->pointer and other casts produce a new
/// value with no tracked origin and are not stripped. Neither is dynamic_cast:
/// it returns null when the runtime type check fails, so a non-null source can
/// yield a null result and dynamic_cast<T*>(p)->f must still warn.
static const Expr *lookThroughPtrToPtrCasts(const Expr *E) {
  while (const auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
    if (isa<CXXDynamicCastExpr>(CE))
      break;
    const Expr *Sub = CE->getSubExpr()->IgnoreParenImpCasts();
    if (!CE->getType()->isPointerType() || !Sub->getType()->isPointerType())
      break;
    E = Sub;
  }
  return E;
}

/// Strip explicit casts, stopping at dynamic_cast (which can yield null from
/// a non-null source, so provenance does not carry through it). Unlike
/// lookThroughPtrToPtrCasts this does not require pointer types on both
/// sides. *FoundCast is set when any explicit cast was seen.
static const Expr *stripNonDynamicCasts(const Expr *E,
                                        bool *FoundCast = nullptr) {
  while (const auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
    if (FoundCast)
      *FoundCast = true;
    if (isa<CXXDynamicCastExpr>(CE))
      break;
    E = CE->getSubExpr()->IgnoreParenImpCasts();
  }
  return E;
}

/// Extract the rightmost leaf of a && / || chain. The CFG gives each operand
/// its own block but every such block carries the full a && b && c as its
/// terminator; the leaf actually evaluated in the last block is the RHS.
static const Expr *getTerminalCondition(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr)
      return getTerminalCondition(BO->getRHS());
  }
  return E;
}

//===----------------------------------------------------------------------===//
// Type and stdlib helpers
//===----------------------------------------------------------------------===//

/// Whether a declared type may be null: an explicit _Nullable always is, and
/// _Null_unspecified is only under -fnullability-default=nullable (the
/// nonnull default treats it as non-null).
static bool isNullableType(QualType Ty, NullabilityKind Default) {
  NullabilityKindOrNone Nullability = Ty->getNullability();
  if (!Nullability)
    return false;
  if (*Nullability == NullabilityKind::Nullable)
    return true;
  if (*Nullability == NullabilityKind::Unspecified &&
      Default == NullabilityKind::Nullable)
    return true;
  return false;
}

/// Template argument \p Idx of the sugared specialization type underlying
/// \p Ty (references and one level of pointer stripped), or a null QualType.
/// Reads the sugar rather than the ClassTemplateSpecializationDecl because
/// the latter strips nullability from its TemplateArguments.
static QualType getSugaredTemplateArgType(QualType Ty, unsigned Idx) {
  Ty = Ty.getNonReferenceType();
  if (Ty->isPointerType())
    Ty = Ty->getPointeeType();
  const auto *TST = Ty->getAs<TemplateSpecializationType>();
  if (!TST)
    return QualType();
  auto Args = TST->template_arguments();
  if (Idx >= Args.size() || Args[Idx].getKind() != TemplateArgument::Type)
    return QualType();
  return Args[Idx].getAsType();
}

/// Recover nullability for a field whose type came from a template argument:
/// Box<int*_Nullable>::val has type int* in the AST. The sugar survives only
/// on the base expression's type, since ClassTemplateSpecializationDecl strips
/// it from its TemplateArguments.
static QualType getTemplateArgTypeForField(const MemberExpr *ME) {
  const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
  if (!FD)
    return QualType();
  const auto *RD = dyn_cast<CXXRecordDecl>(FD->getParent());
  if (!RD)
    return QualType();
  const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
  if (!CTSD)
    return QualType();
  CXXRecordDecl *Pattern = CTSD->getSpecializedTemplate()->getTemplatedDecl();
  for (const auto *PatternField : Pattern->fields()) {
    if (PatternField->getDeclName() != FD->getDeclName())
      continue;
    const auto *TTPT = PatternField->getType()->getAs<TemplateTypeParmType>();
    if (!TTPT || TTPT->getDepth() != 0)
      return QualType();
    return getSugaredTemplateArgType(
        ME->getBase()->IgnoreParenImpCasts()->getType(), TTPT->getIndex());
  }
  return QualType();
}

/// Like getTemplateArgTypeForField, for method return types: when
/// Container<int*_Nullable> has method T get(), the instantiated return type
/// is int* but the template argument carries _Nullable.
static QualType
getTemplateArgTypeForMethodReturn(const CXXMemberCallExpr *MCE) {
  const auto *MD = MCE->getMethodDecl();
  if (!MD)
    return QualType();
  // Case 1: member function template (e.g. S::get<int*_Nullable>()).
  // The template args are on the function itself. Get the sugared args
  // from the MemberExpr in the call, not from the FunctionDecl (which
  // strips sugar on implicit instantiations).
  if (const auto *FT = MD->getPrimaryTemplate()) {
    const FunctionDecl *Pattern = FT->getTemplatedDecl();
    const auto *TTPT = Pattern->getReturnType()->getAs<TemplateTypeParmType>();
    if (TTPT && TTPT->getDepth() == 0) {
      unsigned ArgIdx = TTPT->getIndex();
      const auto *ME = dyn_cast<MemberExpr>(MCE->getCallee());
      if (ME && ME->hasExplicitTemplateArgs()) {
        auto Args = ME->template_arguments();
        if (ArgIdx < Args.size() &&
            Args[ArgIdx].getArgument().getKind() == TemplateArgument::Type)
          return Args[ArgIdx].getArgument().getAsType();
      }
    }
  }

  // Case 2: class template method (e.g. Container<int*_Nullable>::get()).
  const auto *RD = dyn_cast<CXXRecordDecl>(MD->getParent());
  if (!RD)
    return QualType();
  const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
  if (!CTSD)
    return QualType();
  const FunctionDecl *PatternMethod = MD->getInstantiatedFromMemberFunction();
  if (!PatternMethod) {
    // Fallback for methods not directly instantiated (e.g. inherited).
    CXXRecordDecl *Pattern = CTSD->getSpecializedTemplate()->getTemplatedDecl();
    for (const auto *PM : Pattern->methods()) {
      if (PM->getDeclName() == MD->getDeclName() &&
          PM->getNumParams() == MD->getNumParams()) {
        PatternMethod = PM;
        break;
      }
    }
  }
  if (PatternMethod) {
    const auto *TTPT =
        PatternMethod->getReturnType()->getAs<TemplateTypeParmType>();
    if (!TTPT || TTPT->getDepth() != 0)
      return QualType();
    // IgnoreParenImpCasts to see through the const-qualification cast that
    // strips template argument sugar.
    const Expr *Obj = MCE->getImplicitObjectArgument();
    if (!Obj)
      return QualType();
    return getSugaredTemplateArgType(Obj->IgnoreParenImpCasts()->getType(),
                                     TTPT->getIndex());
  }
  return QualType();
}

/// Returns true only for explicitly _Nullable types, NOT for unspecified
/// (unannotated) types that are merely defaulted to nullable. Used for
/// evidence emission to avoid inferring _Nullable from unannotated sources.
static bool isExplicitlyNullableType(QualType Ty) {
  NullabilityKindOrNone Nullability = Ty->getNullability();
  return Nullability && *Nullability == NullabilityKind::Nullable;
}

/// Whether a declared type is explicitly _Nonnull.
static bool isNonnullType(QualType Ty) {
  NullabilityKindOrNone Nullability = Ty->getNullability();
  return Nullability && *Nullability == NullabilityKind::NonNull;
}

/// Walk from a smart pointer expression back to its declaration (if any)
/// and check whether the declared type carries a _Nonnull qualifier.
/// Needed because overload resolution on operator->/operator* strips
/// the nullability attribute from Obj->getType().
static bool isSmartPointerDeclaredNonnull(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      return isNonnullType(VD->getType());
  } else if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl()))
      return isNonnullType(FD->getType());
  }
  return false;
}

/// Check if a type is std::unique_ptr, std::shared_ptr, or std::weak_ptr.
/// Uses getAsCXXRecordDecl() which operates on the canonical type, so
/// type aliases (using/typedef) are handled. Does not match non-std
/// smart pointers (e.g. boost::shared_ptr).
static bool isSmartPointerType(QualType Ty) {
  const auto *RD = Ty->getAsCXXRecordDecl();
  if (!RD)
    return false;
  const auto *DC = RD->getDeclContext();
  if (!DC || !DC->isStdNamespace())
    return false;
  StringRef Name = RD->getName();
  return Name == "unique_ptr" || Name == "shared_ptr" || Name == "weak_ptr";
}

/// Strip implicit wrappers that real standard library headers introduce
/// around expressions (ExprWithCleanups, CXXBindTemporaryExpr,
/// MaterializeTemporaryExpr) plus the usual parens and implicit casts.
static const Expr *unwrapImplicitWrappers(const Expr *E) {
  while (true) {
    E = E->IgnoreParenImpCasts();
    if (const auto *EWC = dyn_cast<ExprWithCleanups>(E))
      E = EWC->getSubExpr();
    else if (const auto *BTE = dyn_cast<CXXBindTemporaryExpr>(E))
      E = BTE->getSubExpr();
    else if (const auto *MTE = dyn_cast<MaterializeTemporaryExpr>(E))
      E = MTE->getSubExpr();
    else
      break;
  }
  return E;
}

/// Check if a smart pointer is constructed from a provably non-null source:
/// make_unique/make_shared, or a constructor taking a new-expression.
static bool isNonnullSmartPtrInit(const Expr *E) {
  E = unwrapImplicitWrappers(E);
  if (const auto *CE = dyn_cast<CXXConstructExpr>(E)) {
    if (CE->getNumArgs() == 1)
      return isNonnullSmartPtrInit(CE->getArg(0));
  }
  // unique_ptr<T>(new T()) wraps the constructor in a functional cast node.
  if (const auto *FCE = dyn_cast<CXXFunctionalCastExpr>(E))
    return isNonnullSmartPtrInit(FCE->getSubExpr());
  // new T(): throwing operator new never returns null.
  if (const auto *NE = dyn_cast<CXXNewExpr>(E))
    return !NE->shouldNullCheckAllocation();
  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    if (const auto *Callee = CE->getDirectCallee()) {
      const auto *DC = Callee->getDeclContext();
      if (DC && DC->isStdNamespace() && Callee->getDeclName().isIdentifier()) {
        StringRef Name = Callee->getName();
        return Name == "make_unique" || Name == "make_shared";
      }
    }
  }
  return false;
}

/// Whether a smart pointer VarDecl is initialized as *it with it =
/// container.begin() (range-for desugaring or a manual loop) on a container
/// whose element type carries _Nonnull. Instantiation strips _Nonnull from
/// non-pointer types like unique_ptr, so the container's sugar type is the
/// only place the annotation survives.
static bool isInitFromNonnullContainerElement(const VarDecl *VD) {
  if (!VD->hasInit())
    return false;
  const Expr *Init = unwrapImplicitWrappers(VD->getInit());

  const auto *OpCall = dyn_cast<CXXOperatorCallExpr>(Init);
  if (!OpCall || OpCall->getOperator() != OO_Star || OpCall->getNumArgs() < 1)
    return false;

  const Expr *IterExpr = OpCall->getArg(0)->IgnoreParenImpCasts();
  const auto *IterDRE = dyn_cast<DeclRefExpr>(IterExpr);
  if (!IterDRE)
    return false;
  const auto *IterVD = dyn_cast<VarDecl>(IterDRE->getDecl());
  if (!IterVD || !IterVD->hasInit())
    return false;

  const Expr *IterInit = unwrapImplicitWrappers(IterVD->getInit());
  const auto *BeginCall = dyn_cast<CXXMemberCallExpr>(IterInit);
  if (!BeginCall)
    return false;

  // Use the declared type: the implicit const cast on .begin()'s object arg
  // strips the sugar.
  const Expr *ObjArg = BeginCall->getImplicitObjectArgument();
  if (!ObjArg)
    return false;
  const Expr *ContainerExpr = ObjArg->IgnoreParenImpCasts();
  QualType ContainerType;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(ContainerExpr)) {
    if (const auto *CVD = dyn_cast<VarDecl>(DRE->getDecl()))
      ContainerType = CVD->getType().getNonReferenceType();
  } else if (const auto *ME = dyn_cast<MemberExpr>(ContainerExpr)) {
    if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl()))
      ContainerType = FD->getType().getNonReferenceType();
  }
  // A pointer to a container (auto it = v->begin()) has never matched here,
  // and getSugaredTemplateArgType would look through the pointer, so reject
  // it up front.
  if (ContainerType.isNull() || ContainerType->isPointerType())
    return false;

  QualType ElemType = getSugaredTemplateArgType(ContainerType, 0);
  if (ElemType.isNull())
    return false;
  return isSmartPointerType(ElemType) && isNonnullType(ElemType);
}

/// Allowlist of STL methods whose unannotated pointer returns are
/// contractually non-null (vector/string/array/span data() and iterators,
/// optional::operator->). Overlay headers cannot redeclare these, so the
/// analysis recognizes them directly; the body is the exact set.
static bool isStlNonnullReturnCall(const CallExpr *CE) {
  const auto *MCE = dyn_cast<CXXMemberCallExpr>(CE);
  if (!MCE)
    return false;
  const auto *MD = MCE->getMethodDecl();
  if (!MD)
    return false;
  if (!MD->getReturnType()->isPointerType())
    return false;
  const auto *RD = MD->getParent();
  if (!RD)
    return false;
  const auto *DC = RD->getDeclContext();
  if (!DC || !DC->isStdNamespace())
    return false;
  StringRef ClassName = RD->getName();

  const auto &DeclName = MD->getDeclName();
  StringRef MethodName;
  bool IsArrowOp = false;
  if (DeclName.isIdentifier()) {
    MethodName = MD->getName();
  } else if (DeclName.getNameKind() == DeclarationName::CXXOperatorName) {
    IsArrowOp = DeclName.getCXXOverloadedOperator() == OO_Arrow;
  }

  // std::vector<T>::data(), begin(), end().
  if (ClassName == "vector")
    return MethodName == "data" || MethodName == "begin" || MethodName == "end";

  // std::basic_string<T> (std::string, std::wstring, ...): c_str(), data(),
  // begin(), end().
  if (ClassName == "basic_string")
    return MethodName == "c_str" || MethodName == "data" ||
           MethodName == "begin" || MethodName == "end";

  // std::basic_string_view<T>::begin(), end(). data() is deliberately absent:
  // a string_view can hold nullptr.
  if (ClassName == "basic_string_view")
    return MethodName == "begin" || MethodName == "end";

  // std::optional<T>::operator->(): UB when empty, so callers assert a value.
  if (ClassName == "optional")
    return IsArrowOp;

  // A non-empty std::array owns inline storage, so these pointers are non-null.
  // std::array<T, 0> is excluded because its data()/iterators may be null.
  if (ClassName == "array" &&
      (MethodName == "data" || MethodName == "begin" || MethodName == "end")) {
    const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    if (!Spec || Spec->getTemplateArgs().size() < 2)
      return false;
    const TemplateArgument &Extent = Spec->getTemplateArgs()[1];
    return Extent.getKind() == TemplateArgument::Integral &&
           !Extent.getAsIntegral().isZero();
  }

  // Prefer silence for std::span's pointer accessors. Empty spans may return a
  // null pointer, but treating every span as nullable produces warnings even
  // for the overwhelmingly common non-empty case.
  if (ClassName == "span")
    return MethodName == "data" || MethodName == "begin" || MethodName == "end";

  return false;
}

/// C library free functions that return null on failure or not-found
/// (malloc, fopen, getenv, strchr, ...). Their results are provably _Nullable
/// regardless of annotations, so unchecked dereferences always warn. Only
/// free functions at global or std scope match; the body is the exact set.
static bool isStdlibNullableReturnCall(const CallExpr *CE) {
  if (isa<CXXMemberCallExpr>(CE))
    return false;
  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return false;
  if (!FD->getReturnType()->isPointerType())
    return false;
  if (isa<CXXMethodDecl>(FD))
    return false;
  const auto &DeclName = FD->getDeclName();
  if (!DeclName.isIdentifier())
    return false;
  // Match only the real C library functions, which live at global scope (or in
  // std, e.g. std::malloc from <cstdlib>). A user function that merely shares
  // the spelling (namespace my { int *malloc(); }) must NOT be treated as
  // nullable; name-only matching would otherwise produce false positives on
  // unrelated code under -fnullability-default=nonnull.
  const DeclContext *DC = FD->getDeclContext()->getRedeclContext();
  if (!DC->isTranslationUnit() && !DC->isStdNamespace())
    return false;
  StringRef Name = FD->getName();
  // Grouped by header (stdlib.h, stdio.h, string.h, ...).
  return llvm::StringSwitch<bool>(Name)
      .Cases({"malloc", "calloc", "realloc", "aligned_alloc"}, true)
      .Cases({"fopen", "freopen", "tmpfile"}, true)
      .Cases({"getenv", "strtok"}, true)
      .Cases({"strstr", "strchr", "strrchr", "strpbrk"}, true)
      .Cases({"memchr", "bsearch"}, true)
      .Cases({"tmpnam", "setlocale"}, true)
      .Default(false);
}

/// The tracked pointer E names, if its declared type is a std smart pointer.
static std::optional<PtrRef> smartPtrRef(const Expr *E) {
  auto R = PtrRef::fromExpr(E);
  if (R && isSmartPointerType(R->getType()))
    return R;
  return std::nullopt;
}

/// True if this std::move(sp) is the init/RHS of a smart-pointer transfer
/// (auto x = std::move(y); or x = std::move(y)). The transfer handler needs
/// the source's pre-move state, and CFG order visits the inner call before the
/// enclosing DeclStmt/operator=, so the context cannot be threaded down. Uses
/// the function-scoped ParentMap, not the TU-wide ASTContext::getParents.
static bool isStdMoveInsideSmartPtrTransferCtx(const CallExpr *CE,
                                               const ParentMap &PM) {
  // A VarDecl initializer's ParentMap parent is the DeclStmt itself. Match the
  // declarator whose init we walked up from; wrapper nodes can make that
  // pointer differ from VD->getInit(), so fall back to the sole declarator.
  auto isSmartPtrInitDecl = [](const DeclStmt *DS, const Stmt *Init) -> bool {
    const VarDecl *Sole = nullptr;
    unsigned NumInited = 0;
    for (const auto *D : DS->decls()) {
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        if (VD->getInit() == Init)
          return isSmartPointerType(VD->getType());
        if (VD->hasInit()) {
          ++NumInited;
          Sole = VD;
        }
      }
    }
    if (NumInited == 1)
      return isSmartPointerType(Sole->getType());
    return false;
  };

  const Stmt *Child = CE;
  for (const Stmt *S = PM.getParent(CE); S; S = PM.getParent(S)) {
    if (const auto *DS = dyn_cast<DeclStmt>(S))
      return isSmartPtrInitDecl(DS, Child);
    if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(S)) {
      if (OCE->getOperator() == OO_Equal && OCE->getNumArgs() >= 2) {
        if (smartPtrRef(OCE->getArg(0)))
          return true;
      }
      return false;
    }
    if (isa<ExprWithCleanups>(S) || isa<CXXBindTemporaryExpr>(S) ||
        isa<MaterializeTemporaryExpr>(S) || isa<ImplicitCastExpr>(S) ||
        isa<ParenExpr>(S) || isa<CXXConstructExpr>(S) ||
        isa<CXXFunctionalCastExpr>(S)) {
      Child = S;
      continue;
    }
    return false;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Condition analysis
//===----------------------------------------------------------------------===//

// Forward declaration: decomposeChain calls analyzeCondition on leaves.
static void
analyzeCondition(const Expr *Cond, ASTContext &Ctx,
                 SmallVectorImpl<ConditionResult> &Results,
                 const NullState::BoolGuardMap *BoolGuards = nullptr);

/// Recursively flatten a chain of Op (BO_LAnd or BO_LOr) operators and
/// analyze each leaf. The && form serves analyzeCondition's !(A && B && C)
/// and guard initializers like ok = a && b; the || form lets the IfStmt
/// level narrow every operand on the false edge of if (A || B).
static void decomposeChain(const Expr *E, BinaryOperatorKind Op,
                           ASTContext &Ctx,
                           SmallVectorImpl<ConditionResult> &Results,
                           const NullState::BoolGuardMap *BoolGuards) {
  E = E->IgnoreParenImpCasts();
  if (const auto *EWC = dyn_cast<ExprWithCleanups>(E))
    E = EWC->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == Op) {
      decomposeChain(BO->getLHS(), Op, Ctx, Results, BoolGuards);
      decomposeChain(BO->getRHS(), Op, Ctx, Results, BoolGuards);
      return;
    }
  }
  analyzeCondition(E, Ctx, Results, BoolGuards);
}

/// !(A && B): the CFG merges the && operand blocks before the if decision,
/// losing their per-operand narrowing, so narrow every operand on the edge
/// where the outer ! is false (all of them held there). Returns true when E
/// was handled.
static bool analyzeNegatedAnd(const Expr *E, bool Negated, ASTContext &Ctx,
                              SmallVectorImpl<ConditionResult> &Results,
                              const NullState::BoolGuardMap *BoolGuards) {
  if (Negated) {
    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_LAnd) {
        // Flatten nested && into a LOCAL vector: Results may already hold
        // leaves appended by an outer decomposeChain (e.g.
        // if (x && !(a && b))), and the erase/flip below must not clobber
        // them.
        SmallVector<ConditionResult, 4> AndResults;
        decomposeChain(BO, BO_LAnd, Ctx, AndResults, BoolGuards);
        // Keep only sub-conditions where the pointer is non-null when the
        // sub-condition is true (Negated=false). Flip to Negated=true so
        // narrowing lands on the false edge of the outer !.
        llvm::erase_if(AndResults,
                       [](const ConditionResult &CR) { return CR.Negated; });
        for (auto &CR : AndResults) {
          CR.Negated = true;
          Results.push_back(std::move(CR));
        }
        return true;
      }
    }
  }
  return false;
}

/// p != nullptr / p == nullptr on a raw pointer, and flag == true / flag != 0
/// on a guard variable. Returns true when BO was such a comparison.
static bool analyzeNullCompare(const BinaryOperator *BO, bool Negated,
                               ASTContext &Ctx,
                               SmallVectorImpl<ConditionResult> &Results,
                               const NullState::BoolGuardMap *BoolGuards) {
  if (BO->getOpcode() == BO_NE || BO->getOpcode() == BO_EQ) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
    bool EqNegated = Negated;
    if (BO->getOpcode() == BO_EQ)
      EqNegated = !EqNegated;

    // Guard variable compared against a constant: flag == true,
    // flag != 0, flag == false. The comparison is true exactly when
    // the guard equals the constant (for ==) or its opposite (for !=), so
    // the stored facts flip when that target truth value is false.
    if (BoolGuards) {
      for (auto [GuardSide, ConstSide] :
           {std::pair{LHS, RHS}, std::pair{RHS, LHS}}) {
        const auto *DRE = dyn_cast<DeclRefExpr>(GuardSide);
        if (!DRE)
          continue;
        const auto *GuardVD = dyn_cast<VarDecl>(DRE->getDecl());
        if (!GuardVD || !GuardVD->getType()->isIntegerType())
          continue;
        auto It = BoolGuards->find(GuardVD);
        if (It == BoolGuards->end())
          continue;
        std::optional<bool> CV = constantTruth(ConstSide, Ctx);
        if (!CV)
          continue;
        // EqNegated means the comparison is effectively == (a bare ==,
        // or != under an odd number of !), so it holds when the guard
        // equals CV; otherwise it holds when the guard equals !CV.
        bool GuardTruthWhenTrue = EqNegated ? *CV : !*CV;
        bool Flip = !GuardTruthWhenTrue;
        for (ConditionResult CR : It->second) {
          CR.Negated = CR.Negated != Flip;
          Results.push_back(std::move(CR));
        }
        return true;
      }
    }

    // IgnoreParenCasts, not ImpCasts: C spells null constants as
    // (T *)0 and that explicit cast is not a null pointer constant in
    // Clang's C mode.
    bool LHSIsNull = LHS->IgnoreParenCasts()->isNullPointerConstant(
        Ctx, Expr::NPC_ValueDependentIsNotNull);
    bool RHSIsNull = RHS->IgnoreParenCasts()->isNullPointerConstant(
        Ctx, Expr::NPC_ValueDependentIsNotNull);

    if (LHSIsNull || RHSIsNull) {
      const Expr *PtrExpr = LHSIsNull ? RHS : LHS;

      // An assignment in the condition, (p = f()) != nullptr, narrows p.
      if (const auto *AssignBO = dyn_cast<BinaryOperator>(PtrExpr)) {
        if (AssignBO->getOpcode() == BO_Assign)
          PtrExpr = AssignBO->getLHS()->IgnoreParenImpCasts();
      }

      // Mirror the read-side cast see-through: (T*)p != nullptr narrows p.
      PtrExpr = lookThroughPtrToPtrCasts(PtrExpr);

      if (auto R = PtrRef::fromExpr(PtrExpr))
        if (R->getType()->isPointerType())
          Results.push_back({std::move(*R), EqNegated});
    }
    return true;
  }
  return false;
}

/// sp != nullptr / sp == nullptr through an overloaded operator on a smart
/// pointer. Returns true when OCE was such a comparison.
static bool
analyzeSmartPtrNullCompare(const CXXOperatorCallExpr *OCE, bool Negated,
                           ASTContext &Ctx,
                           SmallVectorImpl<ConditionResult> &Results) {
  auto OpKind = OCE->getOperator();
  if ((OpKind == OO_ExclaimEqual || OpKind == OO_EqualEqual) &&
      OCE->getNumArgs() == 2) {
    const Expr *LHS = OCE->getArg(0)->IgnoreParenImpCasts();
    const Expr *RHS = OCE->getArg(1)->IgnoreParenImpCasts();

    bool LHSIsNull =
        LHS->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull);
    bool RHSIsNull =
        RHS->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull);

    if (LHSIsNull || RHSIsNull) {
      const Expr *PtrExpr = LHSIsNull ? RHS : LHS;
      PtrExpr = PtrExpr->IgnoreParenImpCasts();
      bool EqNegated = Negated;
      if (OpKind == OO_EqualEqual)
        EqNegated = !EqNegated;

      if (auto R = PtrRef::fromExpr(PtrExpr))
        if (isSmartPointerType(R->getType()))
          Results.push_back({std::move(*R), EqNegated});
    }
    return true;
  }
  return false;
}

/// if (sp) on a smart pointer: an implicit operator bool() call.
static void
analyzeSmartPtrBoolConversion(const CXXMemberCallExpr *MCE, bool Negated,
                              SmallVectorImpl<ConditionResult> &Results) {
  if (const auto *CD =
          dyn_cast_or_null<CXXConversionDecl>(MCE->getMethodDecl())) {
    if (CD->getConversionType()->isBooleanType()) {
      const Expr *Obj = MCE->getImplicitObjectArgument();
      if (Obj && isSmartPointerType(Obj->getType())) {
        // Obj's type was already checked above, so a variable needs no
        // further type test; only a path's leaf field does.
        if (auto R = PtrRef::fromExpr(Obj)) {
          if (R->VD || isSmartPointerType(R->getType())) {
            Results.push_back({std::move(*R), Negated});
            return;
          }
        }
      }
    }
  }
}

/// Analyze a branch condition into ConditionResult facts (see the file
/// overview). The CFG splits && and || operands into their own blocks, but a
/// || operand creating a temporary with a destructor (f() == nullptr on a
/// unique_ptr) gets cleanup blocks that merge the paths before the IfStmt, so
/// decomposeChain re-narrows every operand on the IfStmt's false edge.
static void analyzeCondition(const Expr *Cond, ASTContext &Ctx,
                             SmallVectorImpl<ConditionResult> &Results,
                             const NullState::BoolGuardMap *BoolGuards) {
  if (!Cond)
    return;

  const Expr *E = Cond->IgnoreParenImpCasts();
  E = stripOpaqueValue(E);
  E = unwrapBuiltinExpect(E);
  E = ignoreExplicitBoolCast(E);

  // C++20 rewrites sp != nullptr into !(sp == nullptr) wrapped in a
  // CXXRewrittenBinaryOperator. Unwrap to the semantic form so the ! loop
  // and CXXOperatorCallExpr handler below can process it.
  if (const auto *RBO = dyn_cast<CXXRewrittenBinaryOperator>(E))
    E = RBO->getSemanticForm()->IgnoreParenImpCasts();

  bool Negated = false;
  while (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() != UO_LNot)
      break;
    Negated = !Negated;
    E = ignoreExplicitBoolCast(UO->getSubExpr()->IgnoreParenImpCasts());
  }

  // An explicit pointer-to-pointer cast in the condition (e.g. if ((T*)p))
  // otherwise hides the underlying VarDecl from narrowing. The deref/read side
  // already sees through such casts (lookThroughPtrToPtrCasts at the deref
  // sites), so without this the check side is stricter than the read side and a
  // guarded if ((T*)p) { *p; } produces a false positive.
  E = lookThroughPtrToPtrCasts(E);

  if (analyzeNegatedAnd(E, Negated, Ctx, Results, BoolGuards))
    return;

  if (const auto *BO = dyn_cast<BinaryOperator>(E))
    if (analyzeNullCompare(BO, Negated, Ctx, Results, BoolGuards))
      return;

  // On a smart pointer, sp != nullptr is a CXXOperatorCallExpr.
  if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(E))
    if (analyzeSmartPtrNullCompare(OCE, Negated, Ctx, Results))
      return;

  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_Deref) {
      const Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (VD->getType()->isPointerType()) {
            Results.push_back({PtrRef{VD, std::nullopt}, Negated});
            return;
          }
        }
      }
    }
  }

  // An assignment tested for truth, while ((p = f())), narrows p.
  if (const auto *AssignBO = dyn_cast<BinaryOperator>(E)) {
    if (AssignBO->getOpcode() == BO_Assign)
      E = AssignBO->getLHS()->IgnoreParenImpCasts();
  }

  if (auto R = PtrRef::fromExpr(E)) {
    if (R->VD) {
      const VarDecl *VD = R->VD;
      if (VD->getType()->isPointerType()) {
        Results.push_back({std::move(*R), Negated});
        return;
      }
      // Guard intermediary: if (valid) where valid = (p != nullptr). Integer
      // flags count too (int ok = p != NULL is the C idiom).
      if (BoolGuards && VD->getType()->isIntegerType()) {
        auto It = BoolGuards->find(VD);
        if (It != BoolGuards->end()) {
          // An outer ! flips the sense of every stored fact.
          for (ConditionResult CR : It->second) {
            CR.Negated = CR.Negated != Negated;
            Results.push_back(std::move(CR));
          }
          return;
        }
      }
    } else if (R->getType()->isPointerType()) {
      Results.push_back({std::move(*R), Negated});
      return;
    }
  }

  if (const auto *MCE = dyn_cast<CXXMemberCallExpr>(E))
    analyzeSmartPtrBoolConversion(MCE, Negated, Results);
}

/// Extract the null-check facts a guard variable's initializer or assigned
/// value encodes, so a later if (flag) narrows the pointers it tested. Handles
/// p != nullptr, the ternary spellings p ? true : false / p ? 0 : 1, p && q
/// (guard true means every conjunct held, so only true-direction facts
/// survive), and copies of other guards (bool c = !b).
static void computeGuardFacts(const Expr *Init, ASTContext &Ctx,
                              const NullState::BoolGuardMap &Guards,
                              SmallVectorImpl<ConditionResult> &Facts) {
  Init = Init->IgnoreParenImpCasts();
  if (const auto *ILE = dyn_cast<InitListExpr>(Init)) {
    if (ILE->getNumInits() != 1)
      return;
    Init = ILE->getInit(0)->IgnoreParenImpCasts();
  }
  if (const auto *CO = dyn_cast<AbstractConditionalOperator>(Init)) {
    std::optional<bool> TV = constantTruth(CO->getTrueExpr(), Ctx);
    std::optional<bool> FV = constantTruth(CO->getFalseExpr(), Ctx);
    if (!TV || !FV || *TV == *FV)
      return;
    analyzeCondition(CO->getCond(), Ctx, Facts, &Guards);
    if (!*TV)
      for (auto &CR : Facts)
        CR.Negated = !CR.Negated;
    return;
  }
  if (const auto *BO = dyn_cast<BinaryOperator>(Init)) {
    if (BO->getOpcode() == BO_LAnd) {
      decomposeChain(BO, BO_LAnd, Ctx, Facts, &Guards);
      llvm::erase_if(Facts,
                     [](const ConditionResult &CR) { return CR.Negated; });
      return;
    }
  }
  analyzeCondition(Init, Ctx, Facts, &Guards);
}

/// Narrow a member path, plus every local copied from it
/// (T *q = s->next; if (s->next) *q;).
static void narrowMemberPath(NullState &NS, const MemberAccessPath &Path) {
  NS.markNarrowed(Path);
  for (const auto &[AliasVD, AliasPath] : NS.MemberAliases)
    if (AliasPath == Path)
      NS.markNarrowed(AliasVD);
}

/// Narrow a variable and everything known to hold the same pointer: its
/// alias target and all vars sharing that canonical target (y = x; z = x;
/// if (z) narrows z, x, and y), and the member path it was copied from
/// (T *q = s->next; if (q) *s->next;).
static void narrowVarWithAliases(NullState &NS, const VarDecl *VD) {
  NS.markNarrowed(VD);
  const VarDecl *Target = VD;
  auto AliasIt = NS.Aliases.find(VD);
  if (AliasIt != NS.Aliases.end()) {
    Target = AliasIt->second;
    NS.markNarrowed(Target);
  }
  for (const auto &[AliasVD, AliasTarget] : NS.Aliases)
    if (AliasTarget == VD || AliasTarget == Target)
      NS.markNarrowed(AliasVD);
  auto MemberIt = NS.MemberAliases.find(VD);
  if (MemberIt != NS.MemberAliases.end())
    narrowMemberPath(NS, MemberIt->second);
}

/// Apply one condition fact to a state (the caller picks the edge state
/// matching CR.Negated).
static void applyNarrowing(NullState &NS, const ConditionResult &CR) {
  if (CR.Ref.Path)
    narrowMemberPath(NS, *CR.Ref.Path);
  else if (CR.Ref.VD)
    narrowVarWithAliases(NS, CR.Ref.VD);
}

//===----------------------------------------------------------------------===//
// Transfer functions
//===----------------------------------------------------------------------===//

/// The smart pointer receiver of an sp.get() call, or nullptr when CE is
/// not such a call.
static const Expr *smartPtrGetReceiver(const CallExpr *CE) {
  const auto *MCE = dyn_cast<CXXMemberCallExpr>(CE);
  if (!MCE)
    return nullptr;
  const auto *MD = MCE->getMethodDecl();
  if (!MD || !MD->getDeclName().isIdentifier() || MD->getName() != "get")
    return nullptr;
  const Expr *Obj = MCE->getImplicitObjectArgument();
  if (!Obj || !isSmartPointerType(Obj->getType()))
    return nullptr;
  return Obj;
}

/// Unwrap explicit casts and pointer arithmetic to the original pointer
/// expression. Template instantiations can bake _Nullable into a cast's
/// result type even when the source is unannotated, so when FoundCast is
/// set callers must judge nullability on the source type, not the cast.
/// Stops at a dynamic_cast, which can yield null from a non-null source: the
/// cast itself is then the origin.
static const Expr *unwrapCastsAndArithmetic(const Expr *E, bool &FoundCast) {
  FoundCast = false;
  while (true) {
    if (const auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
      FoundCast = true;
      if (isa<CXXDynamicCastExpr>(CE))
        break;
      E = CE->getSubExpr()->IgnoreParenImpCasts();
    } else if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
        E = BO->getLHS()->getType()->isPointerType()
                ? BO->getLHS()->IgnoreParenImpCasts()
                : BO->getRHS()->IgnoreParenImpCasts();
      } else {
        break;
      }
    } else if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      // *p++ / *++p: the value dereferenced is p (or p+1), so its
      // null-ness is p's.
      if (UO->isIncrementDecrementOp())
        E = UO->getSubExpr()->IgnoreParenImpCasts();
      else
        break;
    } else {
      break;
    }
  }
  return E;
}

namespace {

/// Aggregate of every pointer-valued return seen during one function's
/// fixpoint; feeds the all-returns-nonnull summary emitted afterwards.
struct ReturnSummary {
  bool HasPointerReturn = false;
  bool AllNonnull = true;
};

/// What a smart pointer holds after sp.reset(arg), judged from arg.
enum class ResetNullability { Null, Nonnull, Unknown };

/// Transfer functions for the flow-sensitive nullability dataflow analysis.
/// Processes each CFG statement to update the NullState lattice: tracking
/// narrowing from null checks, invalidation from assignments, and reporting
/// dereferences of nullable pointers via the Handler interface.
class TransferFunctions : public ConstStmtVisitor<TransferFunctions> {
  NullState &State;
  FlowNullabilityHandler &Handler;
  ReturnSummary &Returns;
  ASTContext &Ctx;
  NullabilityKind DefaultNullability;
  // When false, the built-in stdlib nullable-return list (malloc/fopen/...) is
  // ignored (-fno-nullability-stdlib-annotations).
  bool StdlibAnnotations;
  // The enclosing function declaration, needed for return type checking.
  const FunctionDecl *EnclosingFunc;
  // Function-scoped parent map; see isStdMoveInsideSmartPtrTransferCtx.
  const ParentMap *ParentMapPtr;

public:
  TransferFunctions(NullState &State, FlowNullabilityHandler &Handler,
                    ReturnSummary &Returns, ASTContext &Ctx,
                    NullabilityKind DefaultNullability, bool StdlibAnnotations,
                    const FunctionDecl *EnclosingFunc, const ParentMap *PM)
      : State(State), Handler(Handler), Returns(Returns), Ctx(Ctx),
        DefaultNullability(DefaultNullability),
        StdlibAnnotations(StdlibAnnotations), EnclosingFunc(EnclosingFunc),
        ParentMapPtr(PM) {}

  /// Classify each declared pointer, smart pointer, or guard flag from its
  /// initializer.
  void VisitDeclStmt(const DeclStmt *DS) {
    for (const auto *D : DS->decls()) {
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        if (VD->getType()->isPointerType()) {
          handlePointerVarInit(VD);
          continue;
        }
        handleSmartPtrVarInit(VD);
        recordGuardInit(VD);
      }
    }
  }

  /// Pointer arithmetic checks, then assignment handling by LHS shape.
  void VisitBinaryOperator(const BinaryOperator *BO) {
    checkPointerArithmetic(BO);

    if (!BO->isAssignmentOp())
      return;
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();

    handleStoreThroughPointer(LHS);

    if (auto LhsPath = decomposeMemberAccess(LHS))
      handleMemberAssign(BO, *LhsPath);

    if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS))
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        handleVarAssign(BO, VD);
  }

  /// *p dereference checks and p++ / p-- arithmetic checks.
  void VisitUnaryOperator(const UnaryOperator *UO) {
    if (UO->getOpcode() == UO_Deref) {
      const Expr *SubExpr =
          lookThroughPtrToPtrCasts(UO->getSubExpr()->IgnoreParenImpCasts());

      if (const auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (!VD->isImplicit() && !isNarrowed(VD))
            checkVarDeref(UO, VD);
        }
      } else if (const auto *ME = dyn_cast<MemberExpr>(SubExpr)) {
        checkMemberExprDeref(UO, ME);
      } else if (!isa<CXXThisExpr>(SubExpr)) {
        checkExprDeref(UO, SubExpr);
      }
    }

    // p++ / --p: arithmetic on a nullable pointer warns, and the pointer now
    // points elsewhere so every fact about its old value is dropped.
    if (UO->getOpcode() == UO_PostInc || UO->getOpcode() == UO_PreInc ||
        UO->getOpcode() == UO_PostDec || UO->getOpcode() == UO_PreDec) {
      const Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (VD->getType()->isPointerType()) {
            checkVarArithmetic(UO, VD);
            forgetFactsAbout(VD);
          } else if (VD->getType()->isIntegerType()) {
            // A guard flag that changes value no longer encodes the check.
            State.BoolGuards.erase(VD);
          }
        }
      }
    }
  }

  /// p->field dereference checks, including smart pointer operator->.
  void VisitMemberExpr(const MemberExpr *ME) {
    if (!ME->isArrow())
      return;

    const Expr *Base =
        lookThroughPtrToPtrCasts(ME->getBase()->IgnoreParenImpCasts());

    if (isa<CXXThisExpr>(Base))
      return;

    if (checkSmartPtrArrow(ME, Base))
      return;

    if (const auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        if (!isNarrowed(VD))
          checkVarDeref(ME, VD);
      }
    } else if (const auto *BaseME = dyn_cast<MemberExpr>(Base)) {
      checkMemberExprDeref(ME, BaseME);
    } else {
      checkExprDeref(ME, Base);
    }
  }

  /// p[i] dereference checks. An array-typed base or (&x)[i] cannot be null.
  void VisitArraySubscriptExpr(const ArraySubscriptExpr *ASE) {
    const Expr *Base =
        lookThroughPtrToPtrCasts(ASE->getBase()->IgnoreParenImpCasts());
    if (const auto *UO = dyn_cast<UnaryOperator>(Base))
      if (UO->getOpcode() == UO_AddrOf)
        return;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        if (!isNarrowed(VD) && !VD->getType()->isArrayType())
          checkVarDeref(ASE, VD);
      }
    } else if (const auto *ME = dyn_cast<MemberExpr>(Base)) {
      if (!ME->getType()->isArrayType())
        checkMemberExprDeref(ASE, ME);
    } else {
      QualType BaseTy = Base->getType();
      if (!BaseTy->isArrayType())
        checkExprDeref(ASE, Base);
    }
  }

  /// Calls never invalidate narrowing; see the file overview for why.
  void VisitCallExpr(const CallExpr *CE) {
    if (const auto *Callee = CE->getDirectCallee()) {
      narrowFromBuiltinAssume(CE, Callee);
      checkCallArguments(CE, Callee);
    }

    if (const auto *MCE = dyn_cast<CXXMemberCallExpr>(CE))
      handleSmartPtrReset(MCE);

    if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(CE)) {
      handleSmartPtrAssign(OCE);
      // *sp through operator* is checked like operator->.
      if (OCE->getOperator() == OO_Star && OCE->getNumArgs() >= 1) {
        const Expr *Obj = OCE->getArg(0);
        if (isSmartPointerType(Obj->getType()))
          checkSmartPtrDeref(CE, Obj);
      }
    }

    handleBareStdMove(CE);
  }

  /// Check constructor arguments against parameter nullability, same as
  /// VisitCallExpr does for regular function calls.
  void VisitCXXConstructExpr(const CXXConstructExpr *CE) {
    const CXXConstructorDecl *Ctor = CE->getConstructor();
    if (!Ctor)
      return;
    const auto *NNAttr = Ctor->getAttr<NonNullAttr>();
    for (unsigned I = 0, N = std::min(CE->getNumArgs(), Ctor->getNumParams());
         I < N; ++I) {
      const ParmVarDecl *Param = Ctor->getParamDecl(I);
      if (!Param->getType()->isPointerType())
        continue;
      bool ParamIsNonnull =
          isNonnullType(Param->getType()) || (NNAttr && NNAttr->isNonNull(I));
      if (ParamIsNonnull)
        checkNonnullParamArg(CE->getArg(I), Param);
    }
  }

  /// Check aggregate init lists: S{nullptr, &x} where a field is _Nonnull.
  void VisitInitListExpr(const InitListExpr *ILE) {
    const auto *RT = ILE->getType()->getAs<RecordType>();
    if (!RT)
      return;
    const RecordDecl *RD = RT->getDecl();
    if (!RD || (!RD->isStruct() && !RD->isClass()))
      return;
    auto FI = RD->field_begin();
    for (unsigned I = 0, N = ILE->getNumInits(); I < N && FI != RD->field_end();
         ++I, ++FI) {
      const FieldDecl *FD = *FI;
      if (!FD->getType()->isPointerType())
        continue;
      if (!isNonnullType(FD->getType()))
        continue;
      const Expr *Init = ILE->getInit(I)->IgnoreParenImpCasts();
      if (isExprNullable(Init)) {
        ++NumAssignmentWarnings;
        Handler.handleNullableMemberAssignment(ILE->getInit(I), FD);
      }
    }
  }

  /// Return evidence, the all-returns-nonnull summary, and the nullable
  /// return from a _Nonnull function.
  void VisitReturnStmt(const ReturnStmt *RS) {
    if (!EnclosingFunc)
      return;
    const Expr *RetVal = RS->getRetValue();
    if (!RetVal)
      return;
    QualType RetType = EnclosingFunc->getReturnType();
    if (!RetType->isPointerType())
      return;

    // Return evidence is skipped for lambdas and other non-identifier-named
    // functions: they have no cross-TU identity, and getName() would assert.
    bool RetIsNonnull = !isExprNullable(RetVal);
    Returns.HasPointerReturn = true;
    Returns.AllNonnull &= RetIsNonnull;
    if (EnclosingFunc->getDeclName().isIdentifier()) {
      if (RetIsNonnull || isExprNullable(RetVal, /*ExplicitOnly=*/true))
        Handler.handleReturnEvidence(RetVal, EnclosingFunc, RetIsNonnull);
    }

    if (isNonnullType(RetType) && !RetIsNonnull) {
      ++NumReturnWarnings;
      Handler.handleNullableReturn(RetVal, RetVal->getType(), RetType);
    }
  }

private:
  /// Classify a raw pointer variable from its initializer: record aliases,
  /// warn on a nullable init of a _Nonnull variable, then narrow or taint.
  void handlePointerVarInit(const VarDecl *VD) {
    if (VD->hasInit())
      recordPointerSource(VD, VD->getInit());

    if (isNonnullType(VD->getType())) {
      bool InitIsNullable = false;
      if (VD->hasInit()) {
        const Expr *Init = VD->getInit()->IgnoreParenImpCasts();
        // A ternary's merged type inherits _Nullable from either arm
        // even when the condition guards that arm (p ? p : q), so
        // only the arm-aware isExprNullable may judge it.
        bool IsTernary = isa<AbstractConditionalOperator>(Init);
        if (!isExprNarrowedNonnull(Init) && !isNonnullInit(Init) &&
            !isNonnullType(Init->getType()) &&
            ((!IsTernary &&
              isNullableType(Init->getType(), DefaultNullability)) ||
             isExprNullable(Init))) {
          InitIsNullable = true;
          ++NumAssignmentWarnings;
          Handler.handleNullableAssignment(VD->getInit(), VD);
        }
      }
      // A provably nullable init overrides the declared _Nonnull.
      if (InitIsNullable)
        State.markNullable(VD);
      else
        State.markNarrowed(VD);
    } else if (VD->hasInit()) {
      const Expr *Init = VD->getInit()->IgnoreParenImpCasts();
      if (const auto *UO = dyn_cast<UnaryOperator>(Init)) {
        if (UO->getOpcode() == UO_AddrOf)
          narrowAsAddrOf(VD, UO);
      } else if (isExprNarrowedNonnull(Init) || isNonnullInit(Init) ||
                 isNonnullType(Init->getType())) {
        State.markNarrowed(VD);
      } else {
        // Judge the cast SOURCE type: template instantiations can bake
        // _Nullable into a cast's result type even when the source is
        // unannotated (e.g. static_cast<T*>(void_ptr)).
        bool HasCast = false;
        const Expr *TypeExpr = stripNonDynamicCasts(Init, &HasCast);
        // Ternary merged types are judged arm-by-arm (see above).
        bool IsTernary = isa<AbstractConditionalOperator>(TypeExpr);
        if ((!IsTernary &&
             isNullableType(TypeExpr->getType(), DefaultNullability)) ||
            isExprNullable(Init)) {
          State.markNullable(VD);
        } else if (HasCast) {
          // The cast source is not nullable, which overrides any _Nullable
          // baked into the variable's own type by template instantiation.
          State.markNarrowed(VD);
        }
      }
    }
  }

  /// Track smart pointer initialization: narrow if constructed from a
  /// provably non-null source (make_unique, make_shared, new), or inherit
  /// the source's state from auto x = std::move(other).
  void handleSmartPtrVarInit(const VarDecl *VD) {
    // Strip reference: range-for loop variables have type const T&.
    if (isSmartPointerType(VD->getType().getNonReferenceType()) &&
        VD->hasInit()) {
      const Expr *Init = unwrapImplicitWrappers(VD->getInit());
      if (isNonnullSmartPtrInit(Init) ||
          isInitFromNonnullContainerElement(VD)) {
        State.markNarrowed(VD);
      } else {
        // auto x = std::move(other); inherits the source's narrowed
        // state. The standalone std::move handler skipped the source
        // erase (see isStdMoveInsideSmartPtrTransferCtx), so the
        // source's pre-move state is still in NarrowedVars here.
        const Expr *Inner = Init;
        if (const auto *CCE = dyn_cast<CXXConstructExpr>(Inner))
          if (CCE->getNumArgs() == 1)
            Inner = unwrapImplicitWrappers(CCE->getArg(0));
        if (const auto *CE = dyn_cast<CallExpr>(Inner)) {
          if (CE->isCallToStdMove() && CE->getNumArgs() >= 1) {
            if (auto Src = smartPtrRef(CE->getArg(0))) {
              if (State.isNarrowed(*Src))
                State.markNarrowed(VD);
              State.markNullable(*Src);
            }
          }
        }
      }
    }
  }

  /// Track guard variables initialized from null-checks so that
  /// intermediaries like bool valid = (p != nullptr) or
  /// int ok = p ? 1 : 0 later narrow p when used as a condition.
  void recordGuardInit(const VarDecl *VD) {
    if (VD->getType()->isIntegerType() && VD->hasInit()) {
      SmallVector<ConditionResult, 2> Facts;
      computeGuardFacts(VD->getInit(), Ctx, State.BoolGuards, Facts);
      if (!Facts.empty())
        State.BoolGuards[VD] = std::move(Facts);
    }
  }

  /// __builtin_assume(cond) narrows pointers mentioned in cond.
  void narrowFromBuiltinAssume(const CallExpr *CE, const FunctionDecl *Callee) {
    if (Callee->getBuiltinID() == Builtin::BI__builtin_assume &&
        CE->getNumArgs() >= 1) {
      const Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
      SmallVector<ConditionResult, 2> Results;
      analyzeCondition(Arg, Ctx, Results, &State.BoolGuards);
      for (const auto &CR : Results)
        if (!CR.Negated)
          applyNarrowing(State, CR);
    }
  }

  /// Check each argument against its parameter's nullability, then emit
  /// parameter evidence (including nullptr default arguments).
  void checkCallArguments(const CallExpr *CE, const FunctionDecl *Callee) {
    // Narrow pointers passed to _Nonnull or __attribute__((nonnull))
    // parameters: surviving the call proves them non-null. For member
    // operator calls (e.g. lambda operator()) getArg(0) is the implicit
    // object, so real args start at offset 1.
    unsigned ArgOffset = 0;
    if (isa<CXXOperatorCallExpr>(CE) && isa<CXXMethodDecl>(Callee))
      ArgOffset = 1;
    const auto *NNAttr = Callee->getAttr<NonNullAttr>();
    unsigned EffArgs = CE->getNumArgs() - ArgOffset;
    // Parameter evidence for cross-TU inference is skipped for builtins,
    // empty-named functions, and lambda operator() calls (lambda params
    // have no cross-TU identity).
    bool IsLambdaCall = false;
    if (const auto *MD = dyn_cast<CXXMethodDecl>(Callee))
      IsLambdaCall = MD->getParent()->isLambda();
    bool EmitEvidence = !Callee->getBuiltinID() &&
                        !Callee->getDeclName().isEmpty() && !IsLambdaCall;
    for (unsigned I = 0, N = std::min(EffArgs, Callee->getNumParams()); I < N;
         ++I) {
      const ParmVarDecl *Param = Callee->getParamDecl(I);
      if (!Param->getType()->isPointerType())
        continue;
      bool ParamIsNonnull =
          isNonnullType(Param->getType()) || (NNAttr && NNAttr->isNonNull(I));
      // Lambda pointer params default to nonnull (auto-narrowed in body).
      // Verify at call sites: warn when passing nullable to a lambda param
      // that isn't explicitly _Nullable.
      if (!ParamIsNonnull && IsLambdaCall &&
          !isExplicitlyNullableType(Param->getType()))
        ParamIsNonnull = true;
      if (ParamIsNonnull)
        checkNonnullParamArg(CE->getArg(I + ArgOffset), Param);
    }

    // Evidence runs as a second pass so every argument is judged after all
    // nonnull-parameter narrowing from this call has been applied.
    if (EmitEvidence) {
      for (unsigned I = 0, N = std::min(EffArgs, Callee->getNumParams()); I < N;
           ++I) {
        const ParmVarDecl *Param = Callee->getParamDecl(I);
        if (!Param->getType()->isPointerType())
          continue;
        // Skip unnamed parameters: no useful evidence without a name.
        if (!Param->getDeclName().isIdentifier() || Param->getName().empty())
          continue;
        const Expr *Arg = CE->getArg(I + ArgOffset)->IgnoreParenImpCasts();
        bool ArgIsNonnull = !isExprNullable(Arg);
        if (!ArgIsNonnull && !isExprNullable(Arg, /*ExplicitOnly=*/true))
          continue;
        Handler.handleParameterEvidence(CE->getArg(I + ArgOffset), Param,
                                        Callee, ArgIsNonnull);
      }
      // Parameters with nullptr default arguments are nullable evidence
      // even when callers always pass nonnull explicitly: the function
      // can be called without that argument, receiving nullptr.
      for (unsigned I = 0, N = Callee->getNumParams(); I < N; ++I) {
        const ParmVarDecl *Param = Callee->getParamDecl(I);
        if (!Param->getType()->isPointerType() || !Param->hasDefaultArg())
          continue;
        if (Param->hasUninstantiatedDefaultArg())
          continue;
        if (!Param->getDeclName().isIdentifier() || Param->getName().empty())
          continue;
        const Expr *DefArg = Param->getDefaultArg();
        if (DefArg && DefArg->isNullPointerConstant(
                          Ctx, Expr::NPC_ValueDependentIsNotNull))
          Handler.handleParameterEvidence(DefArg, Param, Callee,
                                          /*IsNonnull=*/false);
      }
    }
  }

  /// Handle sp.reset() / sp.reset(ptr), a CXXMemberCallExpr.
  void handleSmartPtrReset(const CXXMemberCallExpr *MCE) {
    const Expr *Obj = MCE->getImplicitObjectArgument();
    if (Obj && isSmartPointerType(Obj->getType())) {
      if (const auto *MD = MCE->getMethodDecl()) {
        if (MD->getDeclName().isIdentifier() && MD->getName() == "reset") {
          // reset(nullptr) makes it null; reset(proven_nonnull) narrows it;
          // an unknown argument clears both facts. libc++ declares
          // reset(pointer p = pointer()), so a no-arg call arrives with a
          // CXXDefaultArgExpr arg, which means reset to null however the
          // default is spelled.
          ResetNullability Result = ResetNullability::Null;
          if (MCE->getNumArgs() > 0) {
            const Expr *Arg = MCE->getArg(0);
            if (!isa<CXXDefaultArgExpr>(Arg)) {
              Arg = Arg->IgnoreParenImpCasts();
              if (Arg->isNullPointerConstant(Ctx,
                                             Expr::NPC_ValueDependentIsNotNull))
                Result = ResetNullability::Null;
              else if (isNonnullInit(Arg))
                Result = ResetNullability::Nonnull;
              else
                Result = ResetNullability::Unknown;
            }
          }
          if (auto R = smartPtrRef(Obj)) {
            State.clear(*R);
            if (Result == ResetNullability::Nonnull)
              State.markNarrowed(*R);
            else if (Result == ResetNullability::Null)
              State.markNullable(*R);
          }
        }
      }
    }
  }

  /// Handle sp = nullptr / sp = make_unique(...) / sp = std::move(other).
  /// LHS may be a local (VarDecl), a this-member, or a member access chain.
  void handleSmartPtrAssign(const CXXOperatorCallExpr *OCE) {
    if (OCE->getOperator() == OO_Equal && OCE->getNumArgs() >= 2) {
      const Expr *LhsArg = OCE->getArg(0);
      std::optional<PtrRef> Lhs = smartPtrRef(LhsArg);

      if (Lhs) {
        // Clear the LHS's proof. A local only loses its narrowing; a member
        // path is additionally marked nullable.
        if (Lhs->VD)
          State.NarrowedVars.erase(Lhs->VD);
        else
          State.markNullable(*Lhs);
        const Expr *RHS = unwrapImplicitWrappers(OCE->getArg(1));

        if (isNonnullSmartPtrInit(RHS)) {
          State.markNarrowed(*Lhs);
        } else if (const auto *RhsCE = dyn_cast<CallExpr>(RHS)) {
          if (RhsCE->isCallToStdMove() && RhsCE->getNumArgs() >= 1) {
            // sp = std::move(other): LHS inherits source's state.
            // Source tracking only implemented for local-var sources.
            auto Src = smartPtrRef(RhsCE->getArg(0));
            if (Src && Src->VD) {
              if (isNarrowed(Src->VD))
                State.markNarrowed(*Lhs);
              State.markNullable(Src->VD);
            }
          } else if (isNonnullType(RhsCE->getType())) {
            State.markNarrowed(*Lhs);
          }
        }
      } else if (auto StructPath = decomposeMemberAccess(LhsArg)) {
        // Non-smart-pointer struct member assignment (e.g. o.inner = fresh):
        // invalidate any narrowed paths nested under the LHS.
        invalidateMembersWithPrefix(*StructPath);
      }
    }
  }

  /// A bare std::move(sp) (an argument, a cast operand, anything but a
  /// smart-pointer transfer) leaves the source moved-from, so it is nullable
  /// until reassigned or null-checked, even when declared _Nonnull. Inside a
  /// smart-pointer transfer (see isStdMoveInsideSmartPtrTransferCtx) the
  /// transfer handler owns the source taint instead; no parent map means "not
  /// a transfer".
  void handleBareStdMove(const CallExpr *CE) {
    if (CE->isCallToStdMove() && CE->getNumArgs() >= 1 &&
        (!ParentMapPtr ||
         !isStdMoveInsideSmartPtrTransferCtx(CE, *ParentMapPtr))) {
      if (auto R = smartPtrRef(CE->getArg(0)))
        State.markNullable(*R);
    }
  }

  /// Warn on arithmetic (p + i, p - i, p += i, p -= i) whose pointer operand
  /// may be null.
  void checkPointerArithmetic(const BinaryOperator *BO) {
    // p + 0 and p - 0 are identity operations and never warn.
    if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
      const Expr *PtrExpr = nullptr;
      const Expr *OtherExpr = nullptr;
      if (BO->getLHS()->getType()->isPointerType()) {
        PtrExpr = BO->getLHS()->IgnoreParenImpCasts();
        OtherExpr = BO->getRHS()->IgnoreParenImpCasts();
      } else if (BO->getRHS()->getType()->isPointerType()) {
        PtrExpr = BO->getRHS()->IgnoreParenImpCasts();
        OtherExpr = BO->getLHS()->IgnoreParenImpCasts();
      }
      if (PtrExpr) {
        bool IsZeroOffset = false;
        if (OtherExpr && !OtherExpr->getType()->isPointerType()) {
          if (auto Val = OtherExpr->getIntegerConstantExpr(Ctx))
            if (*Val == 0)
              IsZeroOffset = true;
        }
        if (!IsZeroOffset) {
          if (const auto *DRE = dyn_cast<DeclRefExpr>(PtrExpr))
            if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
              checkVarArithmetic(BO, VD);
          if (OtherExpr && OtherExpr->getType()->isPointerType())
            if (const auto *DRE = dyn_cast<DeclRefExpr>(OtherExpr))
              if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
                checkVarArithmetic(BO, VD);
        }
      }
    }

    if (BO->getOpcode() == BO_AddAssign || BO->getOpcode() == BO_SubAssign) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      if (LHS->getType()->isPointerType())
        if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS))
          if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
            checkVarArithmetic(BO, VD);
    }
  }

  /// *pp = X where pp is known to hold &local: drop local's narrowing. Only a
  /// precisely known target is invalidated; guessing would flood downstream
  /// dereferences with false positives.
  void handleStoreThroughPointer(const Expr *LHS) {
    if (const auto *UO = dyn_cast<UnaryOperator>(LHS)) {
      if (UO->getOpcode() == UO_Deref) {
        const Expr *Sub = UO->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *SubDRE = dyn_cast<DeclRefExpr>(Sub)) {
          if (const auto *PPVD = dyn_cast<VarDecl>(SubDRE->getDecl())) {
            auto It = State.AddrOfTargets.find(PPVD);
            if (It != State.AddrOfTargets.end()) {
              const VarDecl *TgtVD = It->second;
              // The target loses its narrowing and is not re-narrowed from
              // the RHS.
              State.NarrowedVars.erase(TgtVD);
              State.NullableVars.erase(TgtVD);
              invalidateMembersFor(TgtVD);
              invalidateBoolGuardsFor(TgtVD);
            }
          }
        }
      }
    }
  }

  /// Assignment to a member (this->field, var->field, s.field, or nested
  /// like s.inner.field) invalidates any narrowing on that member, then
  /// re-narrows if the RHS is provably non-null.
  void handleMemberAssign(const BinaryOperator *BO,
                          const MemberAccessPath &LhsPath) {
    const FieldDecl *FD = LhsPath.leafField();
    invalidateMembersWithPrefix(LhsPath);

    if (BO->getOpcode() == BO_Assign && FD->getType()->isPointerType()) {
      const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
      bool Narrowed = isExprNarrowedNonnull(RHS);
      bool IsTernary = isa<AbstractConditionalOperator>(RHS);
      // Null constant assigned to _Nonnull member: warn immediately.
      if (!Narrowed && isNonnullType(FD->getType()) && isExprNullable(RHS) &&
          !isNonnullInit(RHS)) {
        State.markNullable(LhsPath);
        ++NumAssignmentWarnings;
        Handler.handleNullableMemberAssignment(BO, FD);
      } else {
        Narrowed = Narrowed || isNonnullInit(RHS) ||
                   isNonnullType(BO->getRHS()->getType());
        if (Narrowed) {
          State.markNarrowed(LhsPath);
        } else if ((!IsTernary && isNullableType(BO->getRHS()->getType(),
                                                 DefaultNullability)) ||
                   isExprNullable(RHS)) {
          State.markNullable(LhsPath);
        }
      }
      if (Narrowed || isExprNullable(RHS, /*ExplicitOnly=*/true))
        Handler.handleMemberAssignEvidence(BO, FD, Narrowed);
    }
  }

  /// Assignment to a local or parameter: guard flags re-capture their
  /// facts, pointers lose their old facts and are re-classified from the
  /// RHS.
  void handleVarAssign(const BinaryOperator *BO, const VarDecl *VD) {
    // Guard reassignment replaces any stored facts with whatever the
    // new value encodes (ok = p != NULL), or nothing.
    if (VD->getType()->isIntegerType()) {
      State.BoolGuards.erase(VD);
      if (BO->getOpcode() == BO_Assign) {
        SmallVector<ConditionResult, 2> Facts;
        computeGuardFacts(BO->getRHS(), Ctx, State.BoolGuards, Facts);
        if (!Facts.empty())
          State.BoolGuards[VD] = std::move(Facts);
      }
      return;
    }
    if (!VD->getType()->isPointerType())
      return;
    // p += n / p -= n: arithmetic on a non-null pointer stays
    // non-null (and on a nullable one was already diagnosed above), so
    // keep the narrowed/nullable flags and only drop facts that named
    // the old value.
    if (BO->getOpcode() == BO_AddAssign || BO->getOpcode() == BO_SubAssign) {
      forgetFactsAbout(VD);
      return;
    }
    // Self-assignment (p = p, p = (T *)p) changes nothing.
    if (BO->getOpcode() == BO_Assign) {
      const Expr *SelfRHS =
          lookThroughPtrToPtrCasts(BO->getRHS()->IgnoreParenImpCasts());
      if (const auto *RHSDRE = dyn_cast<DeclRefExpr>(SelfRHS))
        if (RHSDRE->getDecl() == VD)
          return;
    }
    State.NarrowedVars.erase(VD);
    State.NullableVars.erase(VD);
    forgetFactsAbout(VD);

    if (BO->getOpcode() == BO_Assign) {
      const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
      recordPointerSource(VD, RHS);

      // Ternary merged types are judged arm-by-arm by
      // isExprNullable/isNonnullInit (see VisitDeclStmt).
      bool IsTernary = isa<AbstractConditionalOperator>(RHS);
      const auto *RHSUO = dyn_cast<UnaryOperator>(RHS);
      if (RHSUO && RHSUO->getOpcode() == UO_AddrOf) {
        narrowAsAddrOf(VD, RHSUO);
      } else if (isExprNarrowedNonnull(RHS)) {
        State.markNarrowed(VD);
      } else if (isNonnullType(VD->getType()) && isExprNullable(RHS) &&
                 !isNonnullInit(RHS)) {
        // Null constant assigned to _Nonnull: warn immediately.
        // Check before isNonnullInit/isNonnullType because implicit
        // casts can propagate _Nonnull from the LHS onto the RHS type.
        State.markNullable(VD);
        ++NumAssignmentWarnings;
        Handler.handleNullableAssignment(BO, VD);
      } else if (isNonnullInit(RHS)) {
        State.markNarrowed(VD);
      } else if (isNonnullType(BO->getRHS()->getType())) {
        State.markNarrowed(VD);
      } else if ((!IsTernary && isNullableType(BO->getRHS()->getType(),
                                               DefaultNullability)) ||
                 isExprNullable(RHS)) {
        State.markNullable(VD);
        if (isNonnullType(VD->getType())) {
          ++NumAssignmentWarnings;
          Handler.handleNullableAssignment(BO, VD);
        }
      }
    }
  }

  /// Whether VD is narrowed (proven non-null) in the current state.
  bool isNarrowed(const VarDecl *VD) const {
    return State.NarrowedVars.contains(VD);
  }

  /// Whether the member path is narrowed in the current state.
  bool isMemberNarrowed(const MemberAccessPath &Path) const {
    return State.NarrowedMembers.contains(Path);
  }

  /// Whether a smart pointer expression (the implicit object of operator->
  /// or operator*) is narrowed in the current state.
  bool isSmartPointerNarrowed(const Expr *E) const {
    auto R = PtrRef::fromExpr(E);
    return R && State.isNarrowed(*R);
  }

  /// Whether a smart pointer expression is flow-marked nullable.
  bool isSmartPointerNullable(const Expr *E) const {
    auto R = PtrRef::fromExpr(E);
    return R && State.isNullable(*R);
  }

  /// Warn on a smart pointer dereference unless the pointer is narrowed or
  /// declared _Nonnull. Flow facts after reset/move override the declared
  /// contract.
  void checkSmartPtrDeref(const Expr *DerefExpr, const Expr *Obj) {
    if (isSmartPointerNullable(Obj) ||
        (!isSmartPointerDeclaredNonnull(Obj) && !isSmartPointerNarrowed(Obj)))
      warnSmartPtrDeref(DerefExpr, Obj);
  }

  /// Returns true when Base (of a -> access) is an overloaded operator->
  /// call, having checked it if the receiver is a smart pointer. Only smart
  /// pointers are checked; other overloaded operator-> (iterators etc.) is
  /// not tracked.
  bool checkSmartPtrArrow(const Expr *DerefExpr, const Expr *Base) {
    const auto *OCE = dyn_cast<CXXOperatorCallExpr>(Base);
    if (!OCE || OCE->getOperator() != OO_Arrow)
      return false;
    if (OCE->getNumArgs() >= 1) {
      const Expr *Obj = OCE->getArg(0);
      if (isSmartPointerType(Obj->getType()))
        checkSmartPtrDeref(DerefExpr, Obj);
    }
    return true;
  }

  /// Gate the built-in stdlib nullable-return list on the langopt so
  /// -fno-nullability-stdlib-annotations fully disables it.
  bool isStdlibNullableReturn(const CallExpr *CE) const {
    return StdlibAnnotations && isStdlibNullableReturnCall(CE);
  }

  /// Count and report a nullable dereference to the handler.
  void reportDeref(const Expr *DerefExpr, QualType PtrType) {
    ++NumDereferenceWarnings;
    Handler.handleNullableDereference(DerefExpr, PtrType);
  }

  /// Report DerefExpr when PtrType (already adjusted by the caller for flow
  /// state and template sugar) is nullable.
  void checkDeref(const Expr *DerefExpr, QualType PtrType) {
    if (isNullableType(PtrType, DefaultNullability)) {
      LLVM_DEBUG(llvm::dbgs()
                 << "  deref: nullable " << PtrType.getAsString() << "\n");
      reportDeref(DerefExpr, PtrType);
    }
  }

  /// Check dereference of a non-variable, non-member expression.
  /// Unwraps casts/arithmetic to avoid template-instantiation false
  /// positives where _Nullable is baked into cast result types.
  void checkExprDeref(const Expr *DerefExpr, const Expr *PtrExpr) {
    bool FoundCast = false;
    const Expr *Origin = unwrapCastsAndArithmetic(PtrExpr, FoundCast);

    // dynamic_cast<T*> yields null on a failed runtime check regardless of the
    // source's nullability, so it is judged as the origin even under further
    // casts or arithmetic. The narrowed idiom
    // if (auto *d = dynamic_cast<T*>(p)) derefs the VarDecl d, not the cast.
    if (const auto *DCE = dyn_cast<CXXDynamicCastExpr>(Origin)) {
      if (DCE->getType()->isPointerType()) {
        reportDeref(DerefExpr, DCE->getType());
        return;
      }
    }

    // *(p ? p : fallback): the merged type of a ternary inherits _Nullable
    // from an arm the condition guards, so judge the arms flow-sensitively.
    if (isa<AbstractConditionalOperator>(Origin)) {
      if (isExprNullable(Origin))
        reportDeref(DerefExpr, PtrExpr->getType());
      return;
    }
    // *p++, *(p + 1): the dereferenced value is p's, so use p's flow
    // state rather than the arithmetic expression's type.
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Origin)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        if (VD->getType()->isPointerType()) {
          if (!isNarrowed(VD))
            checkVarDeref(DerefExpr, VD);
          return;
        }
      }
    }

    // If the origin is inherently non-null, skip.
    if (isa<CXXThisExpr>(Origin))
      return;
    if (const auto *UO = dyn_cast<UnaryOperator>(Origin))
      if (UO->getOpcode() == UO_AddrOf)
        return;
    if (const auto *CE = dyn_cast<CallExpr>(Origin)) {
      // A return type drawn from a nullable template argument overrides the
      // STL and all-returns-nonnull skips.
      bool TemplateOverride = false;
      if (const auto *MCE = dyn_cast<CXXMemberCallExpr>(CE)) {
        QualType ArgTy = getTemplateArgTypeForMethodReturn(MCE);
        if (!ArgTy.isNull() && ArgTy->getNullability())
          TemplateOverride = true;
      }
      if (!TemplateOverride && isStlNonnullReturnCall(CE))
        return;
      if (const auto *Callee = CE->getDirectCallee()) {
        if (!TemplateOverride && Handler.isKnownAllReturnsNonnull(Callee))
          return;
      }
      // sp.get() on a narrowed smart pointer is non-null.
      if (const Expr *Obj = smartPtrGetReceiver(CE))
        if (isSmartPointerNarrowed(Obj))
          return;
    }
    // Throwing operator new never returns null.
    if (const auto *NE = dyn_cast<CXXNewExpr>(Origin)) {
      if (!NE->shouldNullCheckAllocation())
        return;
    }

    if (auto Path = decomposeMemberAccess(Origin)) {
      if (isMemberNarrowed(*Path))
        return;
    }

    QualType CheckTy = FoundCast ? Origin->getType() : PtrExpr->getType();
    // A type without nullability may still get it from a template argument on
    // a method return (Container<int*_Nullable>::get()).
    if (!CheckTy->getNullability()) {
      if (const auto *MCE = dyn_cast<CXXMemberCallExpr>(PtrExpr)) {
        QualType ArgTy = getTemplateArgTypeForMethodReturn(MCE);
        if (!ArgTy.isNull() && ArgTy->getNullability())
          CheckTy = ArgTy;
      }
    }
    checkDeref(DerefExpr, CheckTy);
  }

  /// Report a dereference of VD when its declared type or its flow state is
  /// nullable. Callers test isNarrowed first.
  void checkVarDeref(const Expr *DerefExpr, const VarDecl *VD) {
    QualType Ty = VD->getType();
    if (isNullableType(Ty, DefaultNullability) ||
        State.NullableVars.contains(VD)) {
      LLVM_DEBUG(llvm::dbgs()
                 << "  deref: var '" << VD->getNameAsString() << "'\n");
      reportDeref(DerefExpr, Ty);
    }
  }

  /// Arithmetic on a pointer implies it is valid, so warn when the operand
  /// variable may be null and has not been narrowed.
  void checkVarArithmetic(const Expr *ArithExpr, const VarDecl *VD) {
    if (!isNarrowed(VD) && (isNullableType(VD->getType(), DefaultNullability) ||
                            State.NullableVars.contains(VD))) {
      ++NumArithmeticWarnings;
      Handler.handleNullableArithmetic(ArithExpr, VD->getType());
    }
  }

  /// Report a smart pointer dereference: a local always warns, a var.member
  /// path always warns, and a this->member path warns only when flow marked
  /// it nullable (members set in constructors would otherwise warn).
  void warnSmartPtrDeref(const Expr *DerefExpr, const Expr *Obj) {
    auto R = PtrRef::fromExpr(Obj);
    if (!R)
      return;
    if (R->VD) {
      LLVM_DEBUG(llvm::dbgs() << "  deref: smart ptr '"
                              << R->VD->getNameAsString() << "'\n");
      reportDeref(DerefExpr, R->VD->getType());
      return;
    }
    // Only a smart-pointer-typed leaf field is reported; a variable is
    // reported whatever its declared type.
    if (!isSmartPointerType(R->getType()))
      return;
    if (R->Path->Root || State.NullableMembers.contains(*R->Path))
      reportDeref(DerefExpr, R->getType());
  }

  /// Remove any BoolGuards with a fact about the given pointer variable,
  /// directly or as the root of a member path.
  void invalidateBoolGuardsFor(const VarDecl *VD) {
    State.BoolGuards.remove_if([VD](const auto &Entry) {
      return llvm::any_of(Entry.second, [VD](const ConditionResult &CR) {
        return CR.Ref.VD == VD || (CR.Ref.Path && CR.Ref.Path->Root == VD);
      });
    });
  }

  /// Remove BoolGuards and member aliases that mention a member path under
  /// Prefix (the path was just assigned, so the facts are stale).
  void invalidateGuardsAndAliasesWithPrefix(const MemberAccessPath &Prefix) {
    State.BoolGuards.remove_if([&Prefix](const auto &Entry) {
      return llvm::any_of(Entry.second, [&Prefix](const ConditionResult &CR) {
        return CR.Ref.Path && pathHasPrefix(*CR.Ref.Path, Prefix);
      });
    });
    State.MemberAliases.remove_if([&Prefix](const auto &Entry) {
      return pathHasPrefix(Entry.second, Prefix);
    });
  }

  /// True when the pointer-valued expression is known non-null right now:
  /// a narrowed or _Nonnull variable, or a narrowed member path, seen through
  /// pointer-to-pointer casts (never dynamic_cast).
  bool isExprNarrowedNonnull(const Expr *E) const {
    E = lookThroughPtrToPtrCasts(E->IgnoreParenImpCasts());
    auto R = PtrRef::fromExpr(E);
    if (!R)
      return false;
    if (const VarDecl *VD = R->VD)
      return isNarrowed(VD) ||
             (isNonnullType(VD->getType()) && !State.NullableVars.contains(VD));
    return isMemberNarrowed(*R->Path);
  }

  /// Record VD = <member path> so later narrowing of either side reaches
  /// the other. The path's current narrowed/nullable state is copied onto VD
  /// by the caller's isExprNarrowedNonnull/isExprNullable checks.
  void trackMemberCopy(const VarDecl *VD, const Expr *Init) {
    Init = lookThroughPtrToPtrCasts(Init->IgnoreParenImpCasts());
    auto Path = decomposeMemberAccess(Init);
    if (!Path || !Path->leafField()->getType()->isPointerType())
      return;
    // p = p->next: the path is rooted at the variable being overwritten,
    // so after the store it names a different object. Recording it would
    // let if (p) falsely narrow the new p's next.
    if (Path->Root == VD)
      return;
    State.MemberAliases[VD] = *Path;
  }

  /// Record where VD's new value came from after VD = RHS (or VD's
  /// initializer): an alias to another pointer variable, seen through
  /// explicit casts so T *y = static_cast<T *>(x) also aliases, and a copy
  /// of a member path.
  void recordPointerSource(const VarDecl *VD, const Expr *RHS) {
    RHS = RHS->IgnoreParenImpCasts();
    if (const auto *SrcDRE = dyn_cast<DeclRefExpr>(stripNonDynamicCasts(RHS)))
      if (const auto *SrcVD = dyn_cast<VarDecl>(SrcDRE->getDecl()))
        if (SrcVD->getType()->isPointerType())
          State.Aliases[VD] = resolveAlias(SrcVD);
    trackMemberCopy(VD, RHS);
  }

  /// VD = &local: VD is non-null, and remember the target so a later store
  /// through VD (*VD = x) can drop the target's narrowing.
  void narrowAsAddrOf(const VarDecl *VD, const UnaryOperator *AddrOf) {
    State.markNarrowed(VD);
    if (const auto *TgtDRE =
            dyn_cast<DeclRefExpr>(AddrOf->getSubExpr()->IgnoreParenImpCasts()))
      if (const auto *TgtVD = dyn_cast<VarDecl>(TgtDRE->getDecl()))
        State.AddrOfTargets[VD] = TgtVD;
  }

  /// Remove any Aliases that target the given pointer variable (the alias
  /// source was reassigned, so copies of its old value are stale).
  void invalidateAliasesFor(const VarDecl *VD) {
    State.Aliases.remove_if(
        [VD](const auto &Entry) { return Entry.second == VD; });
  }

  /// The recorded canonical alias target of VD (the map is depth-1), or VD
  /// itself if it is not an alias of anything.
  const VarDecl *resolveAlias(const VarDecl *VD) const {
    auto It = State.Aliases.find(VD);
    return It != State.Aliases.end() ? It->second : VD;
  }

  /// Drop every narrowed/nullable member path and member alias rooted at VD.
  void invalidateMembersFor(const VarDecl *VD) {
    auto RootedAtVD = [VD](const MemberAccessPath &Path) {
      return Path.Root == VD;
    };
    State.NarrowedMembers.remove_if(RootedAtVD);
    State.NullableMembers.remove_if(RootedAtVD);
    State.MemberAliases.remove_if(
        [&](const auto &Entry) { return RootedAtVD(Entry.second); });
  }

  /// Drop every fact that named VD's old value: member paths rooted at it,
  /// guards and aliases that mention it, and its own alias and address-of
  /// records. Does not touch VD's own narrowed/nullable flags.
  void forgetFactsAbout(const VarDecl *VD) {
    invalidateMembersFor(VD);
    invalidateBoolGuardsFor(VD);
    invalidateAliasesFor(VD);
    State.Aliases.erase(VD);
    State.MemberAliases.erase(VD);
    State.AddrOfTargets.erase(VD);
  }

  /// Invalidate all narrowed/nullable member paths that start with Prefix.
  /// e.g. assigning to var.inner invalidates var.inner.x, var.inner.y, etc.
  void invalidateMembersWithPrefix(const MemberAccessPath &Prefix) {
    auto HasPrefix = [&Prefix](const MemberAccessPath &Path) {
      return pathHasPrefix(Path, Prefix);
    };
    State.NarrowedMembers.remove_if(HasPrefix);
    State.NullableMembers.remove_if(HasPrefix);
    invalidateGuardsAndAliasesWithPrefix(Prefix);
  }

  /// Within a ternary Cond ? T : F, an arm can be provably non-null purely
  /// because the condition guards it: p ? p : fallback yields non-null p in
  /// the true arm even though p is nullable in general. Returns whether Cond
  /// narrows the pointer named by Arm to non-null on the branch that selects
  /// it (TrueBranch = the ? arm, otherwise the : arm).
  bool armNarrowedByCondition(const Expr *Arm, const Expr *Cond,
                              bool TrueBranch) const {
    if (!Arm || !Cond)
      return false;
    // GNU p ?: q hands the shared operand to the true arm as an
    // OpaqueValueExpr; look through it to the pointer it names.
    Arm =
        lookThroughPtrToPtrCasts(stripOpaqueValue(Arm->IgnoreParenImpCasts()));
    auto ArmRef = PtrRef::fromExpr(Arm);
    if (!ArmRef)
      return false;
    SmallVector<ConditionResult, 2> Results;
    analyzeCondition(Cond, Ctx, Results, &State.BoolGuards);
    for (const auto &CR : Results) {
      // A true-branch arm needs the pointer non-null when Cond is true
      // (Negated == false); a false-branch arm when Cond is false.
      if (CR.Negated == TrueBranch)
        continue;
      if (*ArmRef == CR.Ref)
        return true;
    }
    return false;
  }

  /// Check if an init expression is provably non-null (address-of, new,
  /// this, _Nonnull typed, narrowed var, cast of non-null, pointer arith).
  bool isNonnullInit(const Expr *Init) const {
    if (!Init)
      return false;
    Init = stripOpaqueValue(Init->IgnoreParenImpCasts());
    // Ternary: both arms must be non-null, either unconditionally or because
    // the condition guards the arm (p ? p : fallback).
    if (const auto *CO = dyn_cast<AbstractConditionalOperator>(Init)) {
      const Expr *Cond = CO->getCond();
      bool TrueOK = isNonnullInit(CO->getTrueExpr()) ||
                    armNarrowedByCondition(CO->getTrueExpr(), Cond, true);
      bool FalseOK = isNonnullInit(CO->getFalseExpr()) ||
                     armNarrowedByCondition(CO->getFalseExpr(), Cond, false);
      return TrueOK && FalseOK;
    }
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        if (isNonnullType(VD->getType()) || isNarrowed(VD))
          return true;
    }
    // Throwing operator new never returns null.
    if (const auto *NE = dyn_cast<CXXNewExpr>(Init)) {
      if (!NE->shouldNullCheckAllocation())
        return true;
    }
    // Pointer dynamic_cast may return null even for a non-null source.
    if (const auto *DCE = dyn_cast<CXXDynamicCastExpr>(Init))
      if (DCE->getType()->isPointerType())
        return false;
    // Other pointer-to-pointer casts preserve null/nonnull status.
    if (const auto *CE = dyn_cast<ExplicitCastExpr>(Init))
      return isNonnullInit(CE->getSubExpr());
    if (isa<CXXThisExpr>(Init))
      return true;
    // Pointer arithmetic on a non-null pointer is non-null.
    if (const auto *BO = dyn_cast<BinaryOperator>(Init)) {
      if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
        if (BO->getLHS()->getType()->isPointerType())
          return isNonnullInit(BO->getLHS()->IgnoreParenImpCasts());
        if (BO->getRHS()->getType()->isPointerType())
          return isNonnullInit(BO->getRHS()->IgnoreParenImpCasts());
      }
    }
    if (const auto *UO = dyn_cast<UnaryOperator>(Init)) {
      if (UO->getOpcode() == UO_AddrOf)
        return true;
    }
    // Call to a function whose every return is proven non-null, or a known
    // STL method that contractually returns nonnull. Stdlib nullable functions
    // (malloc, fopen, etc.) are explicitly excluded.
    if (const auto *CE = dyn_cast<CallExpr>(Init)) {
      if (isStdlibNullableReturn(CE))
        return false;
      if (isStlNonnullReturnCall(CE))
        return true;
      if (const auto *Callee = CE->getDirectCallee()) {
        if (Handler.isKnownAllReturnsNonnull(Callee))
          return true;
      }
      // sp.get() on a narrowed smart pointer returns nonnull. Falls through
      // when the receiver is neither a smart pointer variable nor a member
      // path.
      if (const Expr *Obj = smartPtrGetReceiver(CE)) {
        auto R = PtrRef::fromExpr(Obj);
        if (R && (R->Path || isSmartPointerType(R->getType())))
          return isSmartPointerNarrowed(Obj);
      }
    }
    // Anything else whose own type is _Nonnull (a call to T *_Nonnull f(),
    // a _Nonnull field). Checked last so the flow-sensitive cases above,
    // which can contradict the declared type, take precedence.
    return isNonnullType(Init->getType());
  }

  /// Warn when a nullable pointer reaches a nonnull parameter, then narrow
  /// the argument variable: surviving the call proves it non-null.
  void checkNonnullParamArg(const Expr *ArgExpr, const ParmVarDecl *Param) {
    const Expr *Arg = ArgExpr->IgnoreParenImpCasts();
    if (isExprNullable(Arg)) {
      ++NumArgumentWarnings;
      Handler.handleNullableArgument(ArgExpr, Param);
    }
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Arg)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        if (VD->getType()->isPointerType())
          State.markNarrowed(VD);
    }
  }

  /// Declared-type nullability. With ExplicitOnly only an explicit _Nullable
  /// counts; otherwise _Null_unspecified also counts under
  /// -fnullability-default=nullable.
  bool isNullableByType(QualType Ty, bool ExplicitOnly) const {
    return ExplicitOnly ? isExplicitlyNullableType(Ty)
                        : isNullableType(Ty, DefaultNullability);
  }

  /// Whether E may be null, considering flow. This is the one judgment used
  /// at every site (initializer, assignment, argument, return, dereference of
  /// a ternary), so a tracked pointer is judged by its flow state before its
  /// declared type: narrowing overrides a declared _Nullable and taint
  /// (assigned null, reset, moved-from) overrides a declared _Nonnull.
  /// With ExplicitOnly (used for evidence emission) only provably nullable
  /// sources count: an explicit _Nullable annotation, a null constant, or
  /// flow-tracked nullable state. Unannotated pointers merely defaulted to
  /// nullable do not, so no _Nullable is ever inferred from them, and a
  /// ternary is judged by its merged type.
  bool isExprNullable(const Expr *E, bool ExplicitOnly = false) const {
    if (!E)
      return false;
    E = E->IgnoreParenImpCasts();
    if (!ExplicitOnly)
      E = stripOpaqueValue(E);
    // Null pointer constants (nullptr, NULL, (T*)0) are always nullable.
    // The common type of a ternary like cond ? p : (T*)0 may strip the
    // qualifier, so the arm is judged directly rather than by the
    // expression's type.
    if (E->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
      return true;
    // Pointer dynamic_cast is nullable even when its source is non-null, and
    // under the nonnull default its unannotated result type says nothing, so
    // this rule is what keeps T *q = dynamic_cast<T *>(p) from narrowing q.
    if (const auto *DCE = dyn_cast<CXXDynamicCastExpr>(E))
      if (DCE->getType()->isPointerType())
        return true;
    // Other casts preserve null-ness (static_cast<Base*>(this) is non-null).
    if (const auto *CE = dyn_cast<ExplicitCastExpr>(E))
      return isExprNullable(CE->getSubExpr(), ExplicitOnly);
    // Ternary: nullable iff some arm is nullable and the condition does not
    // guard that arm (p ? p : &x is non-null even though p is nullable,
    // because the true arm is only selected when p tested non-null). The
    // ExplicitOnly form judges a ternary by its merged type below.
    if (!ExplicitOnly) {
      if (const auto *CO = dyn_cast<AbstractConditionalOperator>(E)) {
        const Expr *Cond = CO->getCond();
        bool TrueNullable =
            isExprNullable(CO->getTrueExpr()) &&
            !armNarrowedByCondition(CO->getTrueExpr(), Cond, true);
        bool FalseNullable =
            isExprNullable(CO->getFalseExpr()) &&
            !armNarrowedByCondition(CO->getFalseExpr(), Cond, false);
        return TrueNullable || FalseNullable;
      }
    }
    // A tracked variable or member path: flow state first, then the declared
    // type of the variable or leaf field.
    if (auto R = PtrRef::fromExpr(E)) {
      if (State.isNarrowed(*R))
        return false;
      if (State.isNullable(*R))
        return true;
      return isNullableByType(R->getType(), ExplicitOnly);
    }
    // Throwing operator new never returns null; nothrow new can.
    if (const auto *NE = dyn_cast<CXXNewExpr>(E))
      return NE->shouldNullCheckAllocation();
    if (const auto *CE = dyn_cast<CallExpr>(E)) {
      // Stdlib nullable returns (malloc, fopen, etc.) are provably nullable.
      if (isStdlibNullableReturn(CE))
        return true;
      if (isStlNonnullReturnCall(CE))
        return false;
      if (const auto *Callee = CE->getDirectCallee()) {
        if (Handler.isKnownAllReturnsNonnull(Callee))
          return false;
      }
      // sp.get() follows the smart pointer's flow state. Falls through when
      // the receiver is neither a smart pointer variable nor a member path.
      // An unnarrowed receiver is not provably nullable, so ExplicitOnly
      // falls through to the (unannotated) return type instead.
      if (const Expr *Obj = smartPtrGetReceiver(CE)) {
        auto R = PtrRef::fromExpr(Obj);
        if (R && (R->Path || isSmartPointerType(R->getType()))) {
          if (isSmartPointerNarrowed(Obj))
            return false;
          if (!ExplicitOnly)
            return true;
        }
      }
    }
    if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      if (UO->getOpcode() == UO_AddrOf)
        return false;
    }
    if (isa<CXXThisExpr>(E))
      return false;
    // For non-variable expressions, fall back to the declared type.
    return isNullableByType(E->getType(), ExplicitOnly);
  }

  /// Dereference of a member path (p->q->r): a flow-marked nullable member
  /// overrides its declared _Nonnull, and template-argument sugar supplies
  /// nullability the instantiated field type lost.
  void checkMemberExprDeref(const Expr *DerefExpr, const MemberExpr *ME) {
    const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
    if (checkSmartPtrArrow(DerefExpr, Base))
      return;

    if (auto Path = decomposeMemberAccess(ME)) {
      if (State.NullableMembers.contains(*Path)) {
        reportDeref(DerefExpr, ME->getType());
      } else if (!isMemberNarrowed(*Path)) {
        QualType CheckTy = ME->getType();
        if (!CheckTy->getNullability()) {
          QualType ArgTy = getTemplateArgTypeForField(ME);
          if (!ArgTy.isNull() && ArgTy->getNullability())
            CheckTy = ArgTy;
        }
        checkDeref(DerefExpr, CheckTy);
      }
    }
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Driver
//===----------------------------------------------------------------------===//

/// Seed the analysis's entry state: narrow parameters that are provably
/// non-null on entry (declared _Nonnull, __attribute__((nonnull)), or a
/// lambda's pointer params, which default to nonnull).
static NullState seedEntryState(const Decl *D) {
  NullState InitState;
  const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD)
    return InitState;

  // Collect parameters declared nonnull via __attribute__((nonnull)),
  // either the whole-function form (applies to every pointer param) or
  // the indexed form (nonnull(N...), 1-based).
  llvm::SmallPtrSet<const ParmVarDecl *, 4> AttrNonnull;
  for (const auto *NNA : FD->specific_attrs<NonNullAttr>()) {
    if (NNA->args_size() == 0) {
      for (const auto *P : FD->parameters())
        if (P->getType()->isPointerType())
          AttrNonnull.insert(P);
    } else {
      for (const ParamIdx &Idx : NNA->args()) {
        unsigned I = Idx.getASTIndex();
        if (I < FD->getNumParams())
          AttrNonnull.insert(FD->getParamDecl(I));
      }
    }
  }
  // Lambda pointer params default to nonnull (auto-narrowed). Lambdas are
  // short-lived closures whose callers control what's passed: if a caller
  // passes null, the bug is at the call site (caught by VisitCallExpr's
  // lambda-aware argument check). Explicit _Nullable overrides this default.
  bool IsLambda = false;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD))
    IsLambda = MD->getParent()->isLambda();

  for (const auto *Param : FD->parameters()) {
    if (!Param->getType()->isPointerType())
      continue;
    if (isNonnullType(Param->getType()) || AttrNonnull.contains(Param) ||
        (IsLambda && !isExplicitlyNullableType(Param->getType())))
      InitState.markNarrowed(Param);
  }
  return InitState;
}

/// Emit nonnull/nullable evidence for constructor member initializer lists.
/// These use CXXCtorInitializer (': field(expr)'), not BinaryOperator, so the
/// dataflow's assignment handler never sees them.
static void emitCtorInitEvidence(const Decl *D, ASTContext &Ctx,
                                 const NullState &InitState,
                                 FlowNullabilityHandler &Handler) {
  const auto *CD = dyn_cast_or_null<CXXConstructorDecl>(D);
  if (!CD)
    return;
  for (const auto *CI : CD->inits()) {
    if (!CI->isAnyMemberInitializer())
      continue;
    const FieldDecl *FD = CI->getMember();
    if (!FD || !FD->getType()->isPointerType())
      continue;
    const Expr *Init = CI->getInit();
    if (!Init)
      continue;
    Init = Init->IgnoreParenImpCasts();
    bool IsNonnull = false;
    if (isNonnullType(Init->getType()))
      IsNonnull = true;
    // A parameter narrowed on entry (__attribute__((nonnull)) on the
    // constructor) counts as non-null.
    if (!IsNonnull) {
      if (const auto *DRE = dyn_cast<DeclRefExpr>(Init))
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
          if (InitState.NarrowedVars.contains(VD))
            IsNonnull = true;
    }
    if (!IsNonnull) {
      if (const auto *UO = dyn_cast<UnaryOperator>(Init))
        if (UO->getOpcode() == UO_AddrOf)
          IsNonnull = true;
    }
    if (!IsNonnull) {
      if (const auto *NE = dyn_cast<CXXNewExpr>(Init))
        if (!NE->shouldNullCheckAllocation())
          IsNonnull = true;
    }
    if (!IsNonnull && isa<CXXThisExpr>(Init))
      IsNonnull = true;
    // Nullable evidence needs an explicit source (nullptr or _Nullable), never
    // an unannotated parameter.
    bool IsExplicitlyNullable =
        !IsNonnull &&
        (Init->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull) ||
         isExplicitlyNullableType(Init->getType()));
    if (IsNonnull || IsExplicitlyNullable)
      Handler.handleMemberAssignEvidence(Init, FD, IsNonnull);
  }
}

/// Apply branch-condition narrowing to a block's outgoing edges. Given the
/// block's terminator (an if, loop, &&/||, or ?:) and the state at the
/// end of the block, fill in the per-edge states: the edge that proves a
/// pointer non-null gets it inserted into NarrowedVars. TrueState/FalseState
/// start as copies of the block-exit state and are narrowed in place.
static void narrowOnTerminator(const CFGBlock *Block, const NullState &State,
                               ASTContext &Ctx, NullState &TrueState,
                               NullState &FalseState) {
  const Stmt *Term = Block->getTerminatorStmt();
  if (!Term)
    return;

  const Expr *Cond = nullptr;
  if (const auto *IS = dyn_cast<IfStmt>(Term)) {
    const Expr *IfCond = IS->getCond();
    if (IfCond)
      IfCond = IfCond->IgnoreParenImpCasts();
    if (IfCond) {
      // Unwrap ExprWithCleanups: temp destructors from || RHS
      // expressions wrap the whole condition but don't affect the
      // logical structure.
      const Expr *IfCondInner = IfCond;
      if (const auto *EWC = dyn_cast<ExprWithCleanups>(IfCondInner))
        IfCondInner = EWC->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *BO = dyn_cast<BinaryOperator>(IfCondInner)) {
        if (BO->getOpcode() == BO_LAnd) {
          SmallVector<ConditionResult, 2> AndResults;
          decomposeChain(BO, BO_LAnd, Ctx, AndResults, &State.BoolGuards);
          for (const auto &CR : AndResults)
            if (!CR.Negated)
              applyNarrowing(TrueState, CR);
        } else if (BO->getOpcode() == BO_LOr) {
          // if (A || B): on the false edge ALL operands were false.
          SmallVector<ConditionResult, 2> OrResults;
          decomposeChain(BO, BO_LOr, Ctx, OrResults, &State.BoolGuards);
          for (const auto &CR : OrResults)
            if (CR.Negated)
              applyNarrowing(FalseState, CR);
        }
      }
    }
    Cond = getTerminalCondition(IS->getCond());
  } else if (const auto *WS = dyn_cast<WhileStmt>(Term)) {
    Cond = getTerminalCondition(WS->getCond());
  } else if (const auto *FS = dyn_cast<ForStmt>(Term)) {
    if (FS->getCond())
      Cond = getTerminalCondition(FS->getCond());
  } else if (const auto *DS = dyn_cast<DoStmt>(Term)) {
    Cond = getTerminalCondition(DS->getCond());
  } else if (const auto *BO = dyn_cast<BinaryOperator>(Term)) {
    if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr)
      Cond = getTerminalCondition(BO->getLHS());
  } else if (const auto *CO = dyn_cast<AbstractConditionalOperator>(Term)) {
    // Covers GNU p ?: q (BinaryConditionalOperator) as well.
    Cond = getTerminalCondition(CO->getCond());
  }

  if (Cond) {
    SmallVector<ConditionResult, 2> Results;
    analyzeCondition(Cond, Ctx, Results, &State.BoolGuards);
    for (const auto &CR : Results)
      applyNarrowing(CR.Negated ? FalseState : TrueState, CR);
  }
}

/// After the fixpoint, emit the all-returns-nonnull summary. Skipped if the
/// visit cap fired (a partial run cannot be trusted) and when the return type
/// is explicitly _Nullable: the annotation wins over body inference, since the
/// body may see _Nonnull members that are null at runtime.
static void emitAllReturnsNonnullSummary(const Decl *D, bool HitVisitCap,
                                         const ReturnSummary &Returns,
                                         FlowNullabilityHandler &Handler) {
  if (HitVisitCap)
    return;
  const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD || !FD->getReturnType()->isPointerType() ||
      !FD->getDeclName().isIdentifier())
    return;
  if (Returns.HasPointerReturn && Returns.AllNonnull &&
      !isExplicitlyNullableType(FD->getReturnType()))
    Handler.handleAllReturnsNonnull(FD);
}

void clang::runFlowNullabilityAnalysis(AnalysisDeclContext &AC,
                                       FlowNullabilityHandler &Handler,
                                       NullabilityKind Default,
                                       bool StdlibAnnotations) {
  CFG *Cfg = AC.getCFG();
  if (!Cfg)
    return;

  ReturnSummary Returns;

  ++NumFunctionsAnalyzed;
  ASTContext &Ctx = AC.getASTContext();
  // Cached per function; see isStdMoveInsideSmartPtrTransferCtx.
  const ParentMap &PM = AC.getParentMap();
  LLVM_DEBUG({
    if (const auto *ND = dyn_cast_or_null<NamedDecl>(AC.getDecl()))
      llvm::dbgs() << "flow-nullability: analyzing '" << ND->getNameAsString()
                   << "' (" << Cfg->size() << " blocks)\n";
  });

  using EdgeKey = std::pair<unsigned, unsigned>;
  llvm::DenseMap<EdgeKey, NullState> EdgeStates;
  llvm::DenseMap<unsigned, NullState> BlockEntryStates;

  ForwardDataflowWorklist Worklist(*Cfg, AC);

  const CFGBlock &Entry = Cfg->getEntry();
  const auto *EnclosingFunc = dyn_cast_or_null<FunctionDecl>(AC.getDecl());
  NullState InitState = seedEntryState(AC.getDecl());
  emitCtorInitEvidence(AC.getDecl(), Ctx, InitState, Handler);

  BlockEntryStates[Entry.getBlockID()] = InitState;
  Worklist.enqueueBlock(&Entry);

  // Termination safety net for the non-monotone lattice (see file overview).
  // Generous: well-behaved functions converge in a few visits per block.
  const unsigned MaxBlockVisits = Cfg->size() * 64;
  unsigned BlockVisits = 0;
  bool HitVisitCap = false;
  while (const CFGBlock *Block = Worklist.dequeue()) {
    if (++BlockVisits > MaxBlockVisits) {
      // Diagnostics buffered so far reflect observed states, so keep them.
      ++NumFixpointBailouts;
      HitVisitCap = true;
      break;
    }
    unsigned BlockID = Block->getBlockID();
    ++NumBlocksProcessed;
    LLVM_DEBUG(llvm::dbgs() << "  block B" << BlockID << " (preds:");

    NullState State;
    bool FirstPred = true;

    if (BlockID == Entry.getBlockID()) {
      State = BlockEntryStates[BlockID];
      FirstPred = false;
    }

    for (const CFGBlock *Pred : Block->preds()) {
      if (Pred) {
        LLVM_DEBUG(llvm::dbgs() << " B" << Pred->getBlockID());
        EdgeKey EK = {Pred->getBlockID(), BlockID};
        auto It = EdgeStates.find(EK);
        if (It != EdgeStates.end()) {
          if (FirstPred) {
            State = It->second;
            FirstPred = false;
          } else {
            State = join(State, It->second);
          }
        }
      }
    }
    LLVM_DEBUG(llvm::dbgs() << ")\n");

    if (FirstPred)
      continue;

    // Skip a block whose entry state is unchanged; re-processing it would
    // duplicate warnings. The entry block is exempt because its pre-seeded
    // state would always match.
    if (BlockID != Entry.getBlockID()) {
      auto OldIt = BlockEntryStates.find(BlockID);
      if (OldIt != BlockEntryStates.end() && OldIt->second == State) {
        LLVM_DEBUG(llvm::dbgs() << "    converged, skipping\n");
        continue;
      }
    }
    BlockEntryStates[BlockID] = State;

    TransferFunctions TF(State, Handler, Returns, Ctx, Default,
                         StdlibAnnotations, EnclosingFunc, &PM);
    for (const auto &Elem : *Block) {
      if (std::optional<CFGStmt> CS = Elem.getAs<CFGStmt>())
        if (const Stmt *S = CS->getStmt())
          TF.Visit(S);
    }

    NullState TrueState = State;
    NullState FalseState = State;
    narrowOnTerminator(Block, State, Ctx, TrueState, FalseState);

    for (const auto &[SucIdx, Succ] : llvm::enumerate(Block->succs())) {
      if (const CFGBlock *SuccBlock = Succ) {
        const NullState &SuccState =
            (Block->succ_size() == 2) ? (SucIdx == 0 ? TrueState : FalseState)
                                      : State;
        EdgeKey EK = {BlockID, SuccBlock->getBlockID()};
        auto It = EdgeStates.find(EK);
        if (It == EdgeStates.end() || It->second != SuccState) {
          LLVM_DEBUG(llvm::dbgs()
                     << "    edge B" << BlockID << "->B"
                     << SuccBlock->getBlockID() << " changed, enqueuing\n");
          EdgeStates[EK] = SuccState;
          Worklist.enqueueBlock(SuccBlock);
        }
      }
    }
  }

  emitAllReturnsNonnullSummary(AC.getDecl(), HitVisitCap, Returns, Handler);
}
