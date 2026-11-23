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
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CommandLine.h"

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
    {"returns-fresh", false,
     "%0 returns memory it allocated, but callers are not told, so a use "
     "after they free it goes unchecked; declare it "
     "'__attribute__((ownership_returns(malloc)))' (safec.h: "
     "'__safec_returns_owned')"},
    {"storage-borrow", false,
     "%select{field|global}0 %1 holds a pointer nothing tracks, so it can "
     "outlive what it points to; declare it '__safec_owned' if it is "
     "responsible for freeing its target, or '__safec_static' if it only "
     "points at string literals, globals or functions%select{, or mark the "
     "struct '__safec_view' if it lives only in locals and parameters|}0"},
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

static llvm::cl::opt<bool>
    SafeCOwnership("safe-c-ownership",
                   llvm::cl::desc("Safe C: enforce the M2 ownership rules"),
                   llvm::cl::init(false));

bool clang::safeCOwnershipEnabled(const Sema &S) {
  return S.getLangOpts().SafeC && SafeCOwnership;
}

static bool hasSafeCAnnotation(const Decl *D, StringRef Name) {
  for (const auto *A : D->specific_attrs<AnnotateAttr>())
    if (A->getAnnotation() == Name)
      return true;
  return false;
}

/// A pointer type that would hold a borrow: not a function pointer (functions
/// are never freed). Arrays of pointers count; their elements are stored.
static bool holdsBorrow(QualType T) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  return T->isPointerType() && !T->isFunctionPointerType();
}

void clang::checkSafeCPointerStorage(Sema &S, Decl *D) {
  if (!safeCOwnershipEnabled(S) ||
      S.getSourceManager().isInSystemHeader(D->getLocation()))
    return;
  auto Kinded = [](const Decl *X) {
    return hasSafeCAnnotation(X, "safec_owned") ||
           hasSafeCAnnotation(X, "safec_static") ||
           hasSafeCAnnotation(X, "safec_unchecked");
  };
  if (auto *RD = dyn_cast<RecordDecl>(D)) {
    if (hasSafeCAnnotation(RD, "safec_view"))
      return;
    for (const FieldDecl *FD : RD->fields())
      if (holdsBorrow(FD->getType()) && !Kinded(FD))
        S.SafeCDiag(FD->getLocation(), "storage-borrow") << 0 << FD;
    return;
  }
  if (auto *VD = dyn_cast<VarDecl>(D))
    if (VD->hasGlobalStorage() && !isa<ParmVarDecl>(VD) &&
        holdsBorrow(VD->getType()) && !Kinded(VD))
      S.SafeCDiag(VD->getLocation(), "storage-borrow") << 1 << VD;
}
