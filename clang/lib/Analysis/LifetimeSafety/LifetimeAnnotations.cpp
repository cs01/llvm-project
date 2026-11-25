//===- LifetimeAnnotations.cpp -  -*--------------- C++------------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Analysis/Analyses/LifetimeSafety/LifetimeAnnotations.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/IgnoreExpr.h"
#include "clang/AST/Type.h"
#include "llvm/Support/CommandLine.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Analysis/AnyCall.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/StringSet.h"

namespace clang::lifetimes {

const FunctionDecl *
getDeclWithMergedLifetimeBoundAttrs(const FunctionDecl *FD) {
  return FD != nullptr ? FD->getMostRecentDecl() : nullptr;
}

const CXXMethodDecl *
getDeclWithMergedLifetimeBoundAttrs(const CXXMethodDecl *CMD) {
  const FunctionDecl *FD = CMD;
  return cast_if_present<CXXMethodDecl>(
      getDeclWithMergedLifetimeBoundAttrs(FD));
}

bool isNormalAssignmentOperator(const FunctionDecl *FD) {
  OverloadedOperatorKind OO = FD->getDeclName().getCXXOverloadedOperator();
  bool IsAssignment = OO == OO_Equal || isCompoundAssignmentOperator(OO);
  if (!IsAssignment)
    return false;
  QualType RetT = FD->getReturnType();
  if (!RetT->isLValueReferenceType())
    return false;
  ASTContext &Ctx = FD->getASTContext();
  QualType LHST;
  auto *MD = dyn_cast<CXXMethodDecl>(FD);
  if (MD && MD->isCXXInstanceMember())
    LHST = Ctx.getLValueReferenceType(MD->getFunctionObjectParameterType());
  else
    LHST = FD->getParamDecl(0)->getType();
  return Ctx.hasSameType(RetT, LHST);
}

bool isAssignmentOperatorLifetimeBound(const CXXMethodDecl *CMD) {
  CMD = getDeclWithMergedLifetimeBoundAttrs(CMD);
  return CMD && isNormalAssignmentOperator(CMD) && CMD->param_size() == 1 &&
         CMD->getParamDecl(0)->hasAttr<clang::LifetimeBoundAttr>();
}

/// Check if a function has an attribute on its function type
/// (which represents the implicit 'this' parameter for methods).
/// Returns the attribute if found, nullptr otherwise.
template <typename AttrType>
static const AttrType *getAttrFromFunctionType(const FunctionDecl *FD) {
  const TypeSourceInfo *TSI = FD->getTypeSourceInfo();
  if (!TSI)
    return nullptr;
  TypeLoc TL = TSI->getTypeLoc();
  while (true) {
    auto ATL = TL.getAsAdjusted<AttributedTypeLoc>();
    if (!ATL)
      break;
    if (auto *Attr = ATL.getAttrAs<AttrType>())
      return Attr;
    TL = ATL.getModifiedLoc();
  }
  return nullptr;
}

template <typename AttrType>
static const AttrType *getImplicitObjectParamAttr(const FunctionDecl *FD) {
  // getDeclWithMergedLifetimeBoundAttrs just returns the most recent decl,
  // so it is perfectly safe to reuse for capture_by as well.
  FD = getDeclWithMergedLifetimeBoundAttrs(FD);
  // Attribute merging doesn't work well with attributes on function types (like
  // 'this' param). We need to check all redeclarations.
  auto CheckRedecls = [](const FunctionDecl *F) -> const AttrType * {
    for (const FunctionDecl *Redecl : F->redecls())
      if (const auto *Attr = getAttrFromFunctionType<AttrType>(Redecl))
        return Attr;
    return nullptr;
  };

  if (const auto *Attr = CheckRedecls(FD))
    return Attr;
  if (const FunctionDecl *Pattern = FD->getTemplateInstantiationPattern())
    return CheckRedecls(Pattern);
  return nullptr;
}

const LifetimeBoundAttr *
getDirectImplicitObjectLifetimeBoundAttr(const FunctionDecl *FD) {
  return getAttrFromFunctionType<LifetimeBoundAttr>(FD);
}

const LifetimeBoundAttr *
getImplicitObjectParamLifetimeBoundAttr(const FunctionDecl *FD) {
  return getImplicitObjectParamAttr<LifetimeBoundAttr>(FD);
}

bool implicitObjectParamIsLifetimeBound(const FunctionDecl *FD) {
  if (getImplicitObjectParamLifetimeBoundAttr(FD))
    return true;
  return isNormalAssignmentOperator(FD);
}

bool paramIsLifetimeBound(const ParmVarDecl &PVD) {
  if (PVD.hasAttr<LifetimeBoundAttr>())
    return true;
  return isa<CXXConstructorDecl>(PVD.getDeclContext()) &&
         llvm::any_of(PVD.specific_attrs<LifetimeCaptureByAttr>(),
                      [](const LifetimeCaptureByAttr *A) {
                        return llvm::is_contained(A->params(),
                                                  LifetimeCaptureByAttr::This);
                      });
}

const LifetimeCaptureByAttr *
getCaptureByAttrFromFunctionType(const FunctionDecl *FD) {
  return getImplicitObjectParamAttr<LifetimeCaptureByAttr>(FD);
}

FunctionCallInfo::FunctionCallInfo(const Expr *Call) {
  if (!Call)
    return;

  std::optional<AnyCall> AC = AnyCall::forExpr(ignoreParensAndImpCasts(Call));
  if (!AC)
    return;

  Args = AC->arguments();
  FD = dyn_cast_or_null<FunctionDecl>(AC->getDecl());
}

bool hasObjectArg(const FunctionDecl &FD) {
  const auto *MD = dyn_cast<CXXMethodDecl>(&FD);
  return MD && MD->isInstance() && !isa<CXXConstructorDecl>(MD);
}

bool hasImplicitObjectArg(const FunctionDecl &FD) {
  return hasObjectArg(FD) &&
         cast<CXXMethodDecl>(FD).isImplicitObjectMemberFunction();
}

const ParmVarDecl *getParamForArg(const FunctionDecl &FD, unsigned I) {
  if (hasImplicitObjectArg(FD)) {
    if (I == 0)
      return nullptr;
    --I;
  }
  return I < FD.getNumParams() ? FD.getParamDecl(I) : nullptr;
}

std::optional<LifetimeBoundParamInfo>
getTrackedArgInfo(const FunctionDecl *FD, llvm::ArrayRef<const Expr *> Args,
                  unsigned I) {
  FD = getDeclWithMergedLifetimeBoundAttrs(FD);
  if (!FD || I >= Args.size())
    return std::nullopt;

  if (I == 0 && hasObjectArg(*FD)) {
    const auto *Method = cast<CXXMethodDecl>(FD);
    bool TrackObject =
        implicitObjectParamIsLifetimeBound(Method) ||
        shouldTrackImplicitObjectArg(*Args[0], Method,
                                     /*RunningUnderLifetimeSafety=*/true);
    if (hasImplicitObjectArg(*FD))
      return TrackObject ? std::optional(LifetimeBoundParamInfo(Method))
                         : std::nullopt;
    // An explicit object parameter is reported as the parameter it is.
    if (TrackObject)
      return LifetimeBoundParamInfo(Method->getParamDecl(0));
  }
  if (!hasImplicitObjectArg(*FD)) {
    if (I == 0 && shouldTrackFirstArgument(FD))
      return LifetimeBoundParamInfo(FD->getParamDecl(I));
    if (I == 1 && shouldTrackSecondArgument(FD))
      return LifetimeBoundParamInfo(FD->getParamDecl(I));
  }

  if (const ParmVarDecl *PVD = getParamForArg(*FD, I);
      PVD && paramIsLifetimeBound(*PVD))
    return LifetimeBoundParamInfo(PVD);

  return std::nullopt;
}

std::optional<LifetimeBoundParamInfo>
getTrackingInfoForCallArg(const Expr *Call, const Expr *Source) {
  if (!Call || !Source)
    return std::nullopt;

  auto [FD, Args] = FunctionCallInfo(Call);
  if (!FD)
    return std::nullopt;

  for (unsigned I = 0; I < Args.size(); ++I)
    if (ignoreParensAndImpCasts(Args[I]) == ignoreParensAndImpCasts(Source))
      if (std::optional<LifetimeBoundParamInfo> ParamInfo =
              getTrackedArgInfo(FD, Args, I))
        return ParamInfo;

  return std::nullopt;
}

bool isInStlNamespace(const Decl *D) {
  for (const DeclContext *DC = D->getDeclContext(); DC; DC = DC->getParent()) {
    if (DC->isStdNamespace())
      return true;
    if (const auto *ND = dyn_cast<NamespaceDecl>(DC))
      if (const IdentifierInfo *II = ND->getIdentifier()) {
        StringRef Name = II->getName();
        if (Name.size() >= 2 && Name.front() == '_' &&
            (Name[1] == '_' || isUppercase(Name[1])))
          return true;
      }
  }
  return false;
}

bool isPointerLikeType(QualType QT) {
  return isGslPointerType(QT) || QT->isPointerType() || QT->isNullPtrType();
}

static bool isReferenceOrPointerLikeType(QualType QT) {
  return QT->isReferenceType() || isPointerLikeType(QT);
}

bool shouldTrackImplicitObjectArg(const Expr &ImplicitObjectArgument,
                                  const CXXMethodDecl *Callee,
                                  bool RunningUnderLifetimeSafety) {
  if (!Callee)
    return false;
  // Check both the declaring class and the call-site object: a gsl::Owner
  // may inherit its accessors from a non-Owner base (e.g. libc++ optional).
  const bool IsGslOwnerImplicitObject =
      isGslOwnerType(Callee->getFunctionObjectParameterType()) ||
      (RunningUnderLifetimeSafety &&
       isGslOwnerType(ImplicitObjectArgument.getBestDynamicClassType()));
  if (auto *Conv = dyn_cast<CXXConversionDecl>(Callee))
    if (isGslPointerType(Conv->getConversionType()) && IsGslOwnerImplicitObject)
      return true;
  if (!isGslPointerType(Callee->getFunctionObjectParameterType()) &&
      !IsGslOwnerImplicitObject)
    return false;

  // Begin and end iterators.
  static const llvm::StringSet<> IteratorMembers = {
      "begin", "end", "rbegin", "rend", "cbegin", "cend", "crbegin", "crend"};
  static const llvm::StringSet<> InnerPointerGetters = {
      // Inner pointer getters.
      "c_str", "data", "get"};
  static const llvm::StringSet<> ContainerFindFns = {
      // Map and set types.
      "find", "equal_range", "lower_bound", "upper_bound"};
  // Track dereference operator and transparent functions like begin(), get(),
  // etc. for all GSL pointers. Only do so for lifetime safety analysis and not
  // for Sema's statement-local analysis as it starts to have false-positives.
  if (RunningUnderLifetimeSafety &&
      isGslPointerType(Callee->getFunctionObjectParameterType()) &&
      isReferenceOrPointerLikeType(Callee->getReturnType())) {
    // Propagate origins through GSL pointer arithmetic and dereference
    // operators.
    switch (Callee->getOverloadedOperator()) {
    case OO_Arrow:
    case OO_Star:
    case OO_Plus:
    case OO_Minus:
    case OO_PlusPlus:
    case OO_MinusMinus:
      return true;
    default:
      break;
    }
    if (Callee->getIdentifier() &&
        (IteratorMembers.contains(Callee->getName()) ||
         InnerPointerGetters.contains(Callee->getName())))
      return true;
  }

  if (!isInStlNamespace(Callee->getParent()))
    return false;

  if (isPointerLikeType(Callee->getReturnType())) {
    if (!Callee->getIdentifier())
      // e.g., std::optional<T>::operator->() returns T*.
      return RunningUnderLifetimeSafety
                 ? IsGslOwnerImplicitObject &&
                       Callee->getOverloadedOperator() ==
                           OverloadedOperatorKind::OO_Arrow
                 : false;
    return IteratorMembers.contains(Callee->getName()) ||
           InnerPointerGetters.contains(Callee->getName()) ||
           ContainerFindFns.contains(Callee->getName());
  }
  if (Callee->getReturnType()->isReferenceType()) {
    if (!Callee->getIdentifier()) {
      auto OO = Callee->getOverloadedOperator();
      if (!IsGslOwnerImplicitObject)
        return false;
      return OO == OverloadedOperatorKind::OO_Subscript ||
             OO == OverloadedOperatorKind::OO_Star;
    }
    return llvm::StringSwitch<bool>(Callee->getName())
        .Cases({"front", "back", "at", "top", "value"}, true)
        .Default(false);
  }
  return false;
}

bool shouldTrackFirstArgument(const FunctionDecl *FD) {
  if (!FD->getIdentifier() || FD->getNumParams() < 1)
    return false;
  if (!FD->isInStdNamespace())
    return false;
  // Track std:: algorithm functions that return an iterator whose lifetime is
  // bound to the first argument.
  if (FD->getNumParams() >= 2 && FD->isInStdNamespace() &&
      isGslPointerType(FD->getReturnType())) {
    if (llvm::StringSwitch<bool>(FD->getName())
            .Cases(
                {
                    "find",
                    "find_if",
                    "find_if_not",
                    "find_first_of",
                    "adjacent_find",
                    "search",
                    "find_end",
                    "lower_bound",
                    "upper_bound",
                    "partition_point",
                },
                true)
            .Default(false))
      return true;
  }
  const auto *RD = FD->getParamDecl(0)->getType()->getPointeeCXXRecordDecl();
  if (!RD || !RD->isInStdNamespace())
    return false;
  if (!RD->hasAttr<PointerAttr>() && !RD->hasAttr<OwnerAttr>())
    return false;

  if (FD->getNumParams() != 1)
    return false;

  if (FD->getReturnType()->isPointerType() ||
      isGslPointerType(FD->getReturnType())) {
    return llvm::StringSwitch<bool>(FD->getName())
        .Cases({"begin", "rbegin", "cbegin", "crbegin"}, true)
        .Cases({"end", "rend", "cend", "crend"}, true)
        .Case("data", true)
        .Default(false);
  }
  if (FD->getReturnType()->isReferenceType()) {
    return llvm::StringSwitch<bool>(FD->getName())
        .Cases({"get", "any_cast"}, true)
        .Default(false);
  }
  return false;
}

bool shouldTrackSecondArgument(const FunctionDecl *FD) {
  if (FD->getNumParams() < 2)
    return false;
  const auto *RD = FD->getParamDecl(1)->getType()->getAsCXXRecordDecl();
  if (!RD)
    return false;
  // For free-standing `+`/`-` operators annotated with `gsl::Pointer`, track
  // the second parameter when its type matches the return type.
  return RD->hasAttr<PointerAttr>() &&
         (FD->getOverloadedOperator() == OO_Plus ||
          FD->getOverloadedOperator() == OO_Minus) &&
         ASTContext::hasSameUnqualifiedType(FD->getParamDecl(1)->getType(),
                                            FD->getReturnType()) &&
         !isa<CXXMethodDecl>(FD);
}

template <typename T> static bool isRecordWithAttr(const CXXRecordDecl *RD) {
  if (!RD)
    return false;
  // Generally, if a primary template class declaration is annotated with an
  // attribute, all its specializations generated from template instantiations
  // should inherit the attribute.
  //
  // However, since lifetime analysis occurs during parsing, we may encounter
  // cases where a full definition of the specialization is not required. In
  // such cases, the specialization declaration remains incomplete and lacks the
  // attribute. Therefore, we fall back to checking the primary template class.
  //
  // Note: it is possible for a specialization declaration to have an attribute
  // even if the primary template does not.
  //
  // FIXME: What if the primary template and explicit specialization
  // declarations have conflicting attributes? We should consider diagnosing
  // this scenario.
  bool Result = RD->hasAttr<T>();

  if (auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD))
    Result |= CTSD->getSpecializedTemplate()->getTemplatedDecl()->hasAttr<T>();

  return Result;
}

template <typename T> static bool isRecordWithAttr(QualType Type) {
  return isRecordWithAttr<T>(Type->getAsCXXRecordDecl());
}

bool isGslPointerType(QualType QT) { return isRecordWithAttr<PointerAttr>(QT); }
bool isGslOwnerType(QualType QT) { return isRecordWithAttr<OwnerAttr>(QT); }
bool isGslOwnerType(const CXXRecordDecl *RD) {
  return isRecordWithAttr<OwnerAttr>(RD);
}

bool isOwnerPtrCtor(const CXXConstructorDecl *Ctor, const ParmVarDecl *PVD) {
  return Ctor && PVD->getType()->isPointerType() &&
         isGslOwnerType(Ctor->getParent());
}

static StringRef getName(const CXXRecordDecl &RD) {
  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(&RD))
    return CTSD->getSpecializedTemplate()->getName();
  if (RD.getIdentifier())
    return RD.getName();
  return "";
}

static StringRef getName(const FunctionDecl &FD) {
  if (FD.getIdentifier())
    return FD.getName();
  return "";
}

static bool isStdUniquePtr(const CXXRecordDecl &RD) {
  return RD.isInStdNamespace() && getName(RD) == "unique_ptr";
}

bool isUniquePtrRelease(const CXXMethodDecl &MD) {
  return MD.getIdentifier() && MD.getName() == "release" &&
         MD.getNumParams() == 0 && isStdUniquePtr(*MD.getParent());
}

bool isInvalidationMethod(const CXXMethodDecl &MD) {
  const CXXRecordDecl *RD = MD.getParent();
  if (!isInStlNamespace(RD))
    return false;

  // `pop_back` is excluded: it only invalidates references to the removed
  // element, not to other elements.
  static const llvm::StringSet<> Vector = {// Insertion
                                           "insert", "emplace", "emplace_back",
                                           "push_back", "insert_range",
                                           "append_range",
                                           // Removal
                                           "erase", "clear",
                                           // Memory management
                                           "reserve", "resize", "shrink_to_fit",
                                           // Assignment
                                           "assign", "assign_range"};

  // `pop_*` methods are excluded: they only invalidate references to the
  // removed element, not to other elements.
  static const llvm::StringSet<> Deque = {// Insertion
                                          "insert", "emplace", "insert_range",
                                          // Removal
                                          "erase", "clear",
                                          // Memory management
                                          "resize", "shrink_to_fit",
                                          // Assignment
                                          "assign", "assign_range"};

  static const llvm::StringSet<> String = {
      // Insertion
      "insert", "push_back", "append", "replace", "replace_with_range",
      "insert_range", "append_range",
      // Removal
      "pop_back", "erase", "clear",
      // Memory management
      "reserve", "resize", "resize_and_overwrite", "shrink_to_fit",
      // Assignment
      "swap", "assign", "assign_range"};

  // FIXME: Add queue and stack and check for underlying container
  // (e.g. no invalidation for std::list).
  static const llvm::StringSet<> PriorityQueue = {// Insertion
                                                  "push", "emplace",
                                                  "push_range",
                                                  // Removal
                                                  "pop"};

  // `erase` and `extract` are excluded: they only affect the removed element,
  // not to other elements.
  static const llvm::StringSet<> NodeBased = {// Removal
                                              "clear"};

  // For `flat_*` container adaptors, `try_emplace` and `insert_or_assign`
  // only exist on `flat_map`. Listing them here is harmless since the methods
  // won't be found on other types.
  static const llvm::StringSet<> Flat = {// Insertion
                                         "insert", "emplace", "emplace_hint",
                                         "try_emplace", "insert_or_assign",
                                         "insert_range", "merge",
                                         // Removal
                                         "extract", "erase", "clear",
                                         // Assignment
                                         "replace"};

  static const llvm::StringSet<> UniquePtr = {// Reallocation
                                              "reset"};

  const StringRef RecordName = getName(*RD);
  // TODO: Consider caching this lookup by CXXMethodDecl pointer if this
  // StringSwitch becomes a performance bottleneck.
  const llvm::StringSet<> *InvalidatingMethods =
      llvm::StringSwitch<const llvm::StringSet<> *>(RecordName)
          .Case("vector", &Vector)
          .Case("basic_string", &String)
          .Case("deque", &Deque)
          .Case("priority_queue", &PriorityQueue)
          .Cases({"set", "multiset", "map", "multimap", "unordered_set",
                  "unordered_multiset", "unordered_map", "unordered_multimap"},
                 &NodeBased)
          .Cases({"flat_map", "flat_set", "flat_multimap", "flat_multiset"},
                 &Flat)
          .Case("unique_ptr", &UniquePtr)
          .Default(nullptr);

  if (!InvalidatingMethods)
    return false;

  // Handle Operators via OverloadedOperatorKind
  OverloadedOperatorKind OO = MD.getOverloadedOperator();
  if (OO != OO_None) {
    switch (OO) {
    case OO_Equal:     // operator= : Always invalidates (Assignment)
    case OO_PlusEqual: // operator+= : Append (String/Vector)
      return true;
    case OO_Subscript: // operator[] : Invalidation only for
                       // `flat_map` (Insert-or-access).
                       // `map` and `unordered_map` are excluded.
      return RecordName == "flat_map";
    default:
      return false;
    }
  }

  if (!MD.getIdentifier())
    return false;

  return InvalidatingMethods->contains(MD.getName());
}

bool destructsFirstArg(const FunctionDecl &FD) {
  if (isa<CXXDestructorDecl>(FD))
    return true;
  return isInStlNamespace(&FD) && getName(FD) == "destroy_at";
}

/// The C library's allocator by name. getBuiltinID() is 0 under -fno-builtin
/// (and for some targets' declarations), which would hide every allocation.
static bool isCLibraryFunction(const FunctionDecl &FD,
                               std::initializer_list<StringRef> Names) {
  return FD.getIdentifier() && FD.isExternC() &&
         FD.getDeclContext()->getRedeclContext()->isTranslationUnit() &&
         llvm::is_contained(Names, FD.getName());
}

bool isAllocatingFunction(const FunctionDecl &FD) {
  // The C library's handles (FILE, DIR) are allocated and freed like memory.
  if (isCLibraryFunction(FD, {"malloc", "calloc", "realloc", "reallocf",
                              "aligned_alloc", "strdup", "strndup", "wcsdup",
                              "fopen", "fdopen", "popen",
                              "tmpfile", "opendir", "fdopendir"}))
    return true;
  switch (FD.getBuiltinID()) {
  case Builtin::BImalloc:
  case Builtin::BI__builtin_malloc:
  case Builtin::BIcalloc:
  case Builtin::BI__builtin_calloc:
  case Builtin::BIrealloc:
  case Builtin::BI__builtin_realloc:
    return true;
  default:
    break;
  }
  return llvm::any_of(FD.specific_attrs<OwnershipAttr>(),
                      [](const OwnershipAttr *Attr) {
                        return Attr->getOwnKind() == OwnershipAttr::Returns;
                      });
}

bool isFreeingFunction(const FunctionDecl &FD) {
  if (isCLibraryFunction(FD, {"free", "realloc", "reallocf", "fclose",
                              "pclose", "closedir"}))
    return true;
  switch (FD.getBuiltinID()) {
  case Builtin::BIfree:
  case Builtin::BI__builtin_free:
  case Builtin::BIrealloc:
  case Builtin::BI__builtin_realloc:
    return true;
  default:
    break;
  }
  return llvm::any_of(FD.specific_attrs<OwnershipAttr>(),
                      [](const OwnershipAttr *Attr) {
                        return Attr->getOwnKind() == OwnershipAttr::Takes;
                      });
}

bool isStdCallableWrapperType(const CXXRecordDecl *RD) {
  if (!RD || !isInStlNamespace(RD))
    return false;
  StringRef Name = getName(*RD);
  return Name == "function" || Name == "move_only_function";
}

bool isStdReferenceCast(const FunctionDecl *FD) {
  if (!FD)
    return false;
  switch (FD->getBuiltinID()) {
  case Builtin::BImove:
  case Builtin::BImove_if_noexcept:
  case Builtin::BIforward:
  case Builtin::BIforward_like:
  case Builtin::BIas_const:
    return true;
  default:
    return false;
  }
}

const Expr *ignoreParensAndImpCasts(const Expr *E) {
  return IgnoreExprNodes(E, IgnoreParensSingleStep,
                         IgnoreImplicitCastsSingleStep);
}


static llvm::cl::opt<bool>
    SafeCOwnership("safe-c-ownership",
                   llvm::cl::desc("Safe C: enforce the M2 ownership rules (on by "
                                  "default; =false for comparisons)"),
                   llvm::cl::init(true));

bool safeCOwnershipFlag() { return SafeCOwnership; }

// The rules that ask for ownership annotations (who frees what, exclusive and
// shared borrows, thread sharing). The driver turns them off unless asked for;
// the analyses still run, and report what needs no annotation.
static llvm::cl::opt<bool> SafeCStrictOwnership(
    "safe-c-strict-ownership",
    llvm::cl::desc("Safe C: require ownership annotations and report what "
                   "they show"),
    llvm::cl::init(true));
bool safeCStrictOwnership() { return SafeCStrictOwnership; }

static llvm::cl::opt<bool> SafeCExclDefault(
    "safe-c-excl-default",
    llvm::cl::desc("Safe C: an unannotated pointer-to-non-const parameter is "
                   "_Exclusive (Rust's &mut), as `const T *` is a shared borrow"),
    llvm::cl::init(false));

llvm::StringRef safeCParamKind(QualType T) {
  llvm::StringRef K = safeCPointerKind(T);
  if (!K.empty() || !SafeCExclDefault || !T->isPointerType())
    return K;
  QualType Pointee = T->getPointeeType();
  if (Pointee.isConstQualified() || Pointee->isFunctionType() ||
      Pointee->isVoidType())
    return K;
  return "excl";
}

llvm::StringRef safeCParamDeclKind(const ParmVarDecl *P) {
  // System headers keep their declared meaning: libc's `FILE *` is not
  // an exclusive borrow.
  const SourceManager &SM = P->getASTContext().getSourceManager();
  if (P->getLocation().isValid() && SM.isInSystemHeader(P->getLocation()))
    return safeCPointerKind(P->getType());
  return safeCParamKind(P->getType());
}

llvm::StringRef safeCDeclKind(const ValueDecl *D) {
  if (const auto *P = dyn_cast<ParmVarDecl>(D))
    return safeCParamDeclKind(P);
  return safeCPointerKind(D->getType());
}

const Expr *safeCPeel(const Expr *E) {
  while (true) {
    E = E->IgnoreParenImpCasts();
    const auto *OVE = dyn_cast<OpaqueValueExpr>(E);
    if (!OVE || !OVE->getSourceExpr())
      return E;
    E = OVE->getSourceExpr();
  }
}

bool isSafeCViewType(QualType T) {
  const RecordDecl *RD = T->getAsRecordDecl();
  if (!RD)
    return false;
  for (const auto *A : RD->specific_attrs<AnnotateAttr>())
    if (A->getAnnotation() == "safec_view")
      return true;
  return false;
}

/// What a pointer type promises a caller, beyond its C type: the ownership
/// kind, and its bounds (with the parameters a count names, by position).
std::string safeCPromise(QualType T) {
  std::string Out = safeCPointerKind(T).str();
  // Giving ownership away may be written on the pointer's declaration
  // (_Takes) instead: checkSafeCTakesStore compares that.
  if (Out == "owned")
    Out.clear();
  if (const auto *CAT = T->getAs<CountAttributedType>()) {
    Out += "|count" + std::to_string(int(CAT->getKind())) + ":";
    struct Printer : RecursiveASTVisitor<Printer> {
      std::string Out;
      bool VisitDeclRefExpr(DeclRefExpr *E) {
        if (const auto *P = dyn_cast<ParmVarDecl>(E->getDecl()))
          Out += "#" + std::to_string(P->getFunctionScopeIndex());
        else
          Out += E->getDecl()->getNameAsString();
        Out += ",";
        return true;
      }
      bool VisitIntegerLiteral(IntegerLiteral *E) {
        Out += llvm::toString(E->getValue(), 10, false) + ",";
        return true;
      }
      bool VisitBinaryOperator(BinaryOperator *E) {
        Out += E->getOpcodeStr().str();
        return true;
      }
    } P;
    P.TraverseStmt(CAT->getCountExpr());
    Out += P.Out;
  } else if (T->isBoundsAttributedType()) {
    Out += "|bounds";
  } else if (T->isValueTerminatedType()) {
    Out += "|terminated";
  }
  if (T->isPointerType() && T->getPointeeType()->isPointerType())
    Out += "*" + safeCPromise(T->getPointeeType());
  return Out;
}

static void addRecord(std::string &Out, QualType T, unsigned Depth);
static void addType(std::string &Out, QualType T, unsigned Depth) {
  Out += T.getCanonicalType().getAsString() + safeCPromise(T);
  if (auto N = T->getNullability())
    Out += "|null" + std::to_string(int(*N));
  addRecord(Out, T, Depth);
}
static void addRecord(std::string &Out, QualType T, unsigned Depth) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  if (const auto *ED = T->getAsEnumDecl())
    if (const EnumDecl *Def = ED->getDefinition())
      for (const EnumConstantDecl *C : Def->enumerators())
        Out += "," + C->getNameAsString() + "=" +
               llvm::toString(C->getInitVal(), 10);
  const RecordDecl *RD = T->getAsRecordDecl();
  RD = RD ? RD->getDefinition() : nullptr;
  if (!RD || Depth > 6)
    return;
  Out += "{";
  for (const auto *A : RD->specific_attrs<AnnotateAttr>())
    Out += A->getAnnotation().str();
  for (const FieldDecl *F : RD->fields()) {
    Out += F->getNameAsString() + ":";
    addType(Out, F->getType(), Depth + 1);
    for (const auto *A : F->specific_attrs<AnnotateAttr>()) {
      Out += A->getAnnotation().str();
      for (const Expr *Arg : A->args()) {
        Expr::EvalResult R;
        if (Arg->EvaluateAsInt(R, F->getASTContext()))
          Out += llvm::toString(R.Val.getInt(), 10) + ",";
      }
    }
    Out += ";";
  }
  Out += "}";
}

std::vector<std::pair<std::string, std::string>>
safeCPointedStructs(const ValueDecl *VD) {
  std::vector<std::pair<std::string, std::string>> Out;
  auto Add = [&](QualType T) {
    if (!T->isPointerType())
      return;
    while (T->isPointerType())
      T = T->getPointeeType();
    const TagDecl *TD = T->getAsTagDecl();
    TD = TD ? TD->getDefinition() : nullptr;
    if (!TD || VD->getASTContext().getSourceManager().isInSystemHeader(
                   TD->getLocation()))
      return;
    std::string Name = TD->getNameAsString(), Contents;
    if (Name.empty() && TD->getTypedefNameForAnonDecl())
      Name = TD->getTypedefNameForAnonDecl()->getNameAsString();
    addRecord(Contents, T, 0);
    Out.push_back({Name, Contents});
  };
  if (const auto *FD = dyn_cast<FunctionDecl>(VD)) {
    Add(FD->getReturnType());
    for (const ParmVarDecl *P : FD->parameters())
      Add(P->getType());
  } else {
    Add(VD->getType());
  }
  return Out;
}

std::string safeCSignature(const ValueDecl *VD) {
  std::string Out;
  const auto *FD = dyn_cast<FunctionDecl>(VD);
  if (!FD) {
    addType(Out, VD->getType(), 0);
  } else {
    addType(Out, FD->getReturnType(), 0);
    for (const ParmVarDecl *P : FD->parameters()) {
      Out += "(";
      addType(Out, P->getType(), 0);
      if (P->hasAttr<LifetimeBoundAttr>())
        Out += "|lifetimebound";
      if (P->hasAttr<NoEscapeAttr>())
        Out += "|noescape";
      Out += ")";
    }
    for (const auto *A : FD->specific_attrs<OwnershipAttr>()) {
      Out += A->isReturns() ? "|returns" : "|takes";
      if (!A->isReturns())
        for (const ParamIdx &I : A->args())
          Out += std::to_string(I.getASTIndex());
    }
  }
  // Locks: printed, so the capability expressions count.
  for (const Attr *A : VD->attrs())
    switch (A->getKind()) {
    case attr::GuardedBy:
    case attr::PtGuardedBy:
    case attr::RequiresCapability:
    case attr::AcquireCapability:
    case attr::ReleaseCapability:
    case attr::TryAcquireCapability:
    case attr::AssertCapability:
    case attr::LocksExcluded:
    case attr::ReturnsNonNull:
    case attr::AllocSize: {
      llvm::raw_string_ostream OS(Out);
      A->printPretty(OS, VD->getASTContext().getPrintingPolicy());
      break;
    }
    default:
      break;
    }
  return Out;
}

bool safeCReachesOwner(QualType T, unsigned Depth) {
  if (Depth > 8)
    return true;
  // Pointer kinds are type attributes: read them before any desugaring.
  if (T->isPointerType())
    return safeCPointerKind(T) == "owned";
  if (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    return safeCReachesOwner(AT->getElementType(), Depth + 1);
  if (const RecordDecl *RD = T->getAsRecordDecl()) {
    const RecordDecl *Def = RD->getDefinition();
    if (!Def)
      return true; // opaque: assume it can
    for (const FieldDecl *FD : Def->fields())
      if (safeCReachesOwner(FD->getType(), Depth + 1))
        return true;
  }
  return false;
}

const VarDecl *safeCRootVar(const Expr *E) {
  while (true) {
    E = safeCPeel(E);
    if (const auto *ME = dyn_cast<MemberExpr>(E))
      E = ME->getBase();
    else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E))
      E = ASE->getBase();
    else if (const auto *UO = dyn_cast<UnaryOperator>(E);
             UO && (UO->getOpcode() == UO_Deref || UO->getOpcode() == UO_AddrOf))
      E = UO->getSubExpr();
    else
      break;
  }
  const auto *DRE = dyn_cast<DeclRefExpr>(E);
  const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
  return VD && !VD->hasGlobalStorage() ? VD : nullptr;
}

llvm::StringRef safeCPointerKind(QualType T) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  // Walk this pointer's own sugar (typedefs, bounds and nullability
  // attributes), never into its pointee.
  for (const Type *Cur = T.getTypePtr();;) {
    if (const auto *AT = dyn_cast<AttributedType>(Cur)) {
      if (const auto *A = dyn_cast_or_null<AnnotateTypeAttr>(AT->getAttr()))
        if (A->getAnnotation().starts_with("safec_"))
          return A->getAnnotation().drop_front(6);
      Cur = AT->getModifiedType().getTypePtr();
      continue;
    }
    QualType Next = Cur->getLocallyUnqualifiedSingleStepDesugaredType();
    if (Next.getTypePtr() == Cur)
      return "";
    Cur = Next.getTypePtr();
  }
}

} // namespace clang::lifetimes
