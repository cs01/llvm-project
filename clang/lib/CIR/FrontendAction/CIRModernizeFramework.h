//===--- CIRModernizeFramework.h - Shared parts of CIR-proven rewrites ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A rewrite proposes candidates, each a function parameter with the edits
// that rewrite it, the reason it is blocked if it is, and the other
// candidates it needs. The framework (CIRModernize.cpp) runs every enabled
// rewrite over a translation unit, keeps the candidates whose needs survive,
// and reports, applies or exports them. Project mode, the plan, the merge in
// project.ts and the IR proof in check.ts work the same for every rewrite.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_CIR_FRONTENDACTION_CIRMODERNIZEFRAMEWORK_H
#define CLANG_LIB_CIR_FRONTENDACTION_CIRMODERNIZEFRAMEWORK_H

#include "CIRModernize.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/StringSaver.h"
#include <memory>
#include <optional>
#include <vector>

namespace clang::cirmodernize {

class Collector : public RecursiveASTVisitor<Collector> {
  using Base = RecursiveASTVisitor<Collector>;

public:
  // Instantiations are walked only to see which functions they reference. An
  // edit there would land in the template, which every instantiation shares,
  // so such a function is never a candidate. Nothing inside an instantiation
  // is collected as a use to rewrite.
  bool shouldVisitTemplateInstantiations() const { return true; }

  bool TraverseDecl(Decl *D) {
    bool Inst = D && isInstantiation(D);
    InstantiationDepth += Inst;
    bool Result = Base::TraverseDecl(D);
    InstantiationDepth -= Inst;
    return Result;
  }

  bool dataTraverseStmtPre(Stmt *S) {
    if (!InstantiationDepth)
      return true;
    const ValueDecl *Ref = nullptr;
    if (auto *DRE = dyn_cast<DeclRefExpr>(S))
      Ref = DRE->getDecl();
    else if (auto *ME = dyn_cast<MemberExpr>(S))
      Ref = ME->getMemberDecl();
    if (const auto *FD = dyn_cast_or_null<FunctionDecl>(Ref))
      UsedInInstantiation.insert(FD->getCanonicalDecl());
    return true;
  }

  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (InstantiationDepth)
      return true;
    Declared.insert(FD->getCanonicalDecl());
    if (FD->doesThisDeclarationHaveABody())
      Functions.push_back(FD);
    return true;
  }
  bool VisitCallExpr(CallExpr *CE) {
    if (!InstantiationDepth)
      Calls.push_back(CE);
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (!InstantiationDepth)
      Refs.push_back(DRE);
    return true;
  }
  bool VisitMemberExpr(MemberExpr *ME) {
    if (!InstantiationDepth)
      Members.push_back(ME);
    return true;
  }
  // A dependent call resolves when the template is instantiated, possibly
  // in another translation unit, and its call site cannot be rewritten for
  // one function. Each form can only reach some functions: an unqualified
  // call those lookup already found plus, through ADL, any free function of
  // that name; a member or T:: call, any method of that name.
  bool VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E) {
    addFound(E);
    if (E->requiresADL())
      UnresolvedFree.insert(E->getName());
    return true;
  }
  bool VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
    addFound(E);
    UnresolvedMethods.insert(E->getName());
    return true;
  }
  bool VisitCXXDependentScopeMemberExpr(CXXDependentScopeMemberExpr *E) {
    UnresolvedMethods.insert(E->getMember());
    return true;
  }
  bool VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E) {
    UnresolvedMethods.insert(E->getDeclName());
    return true;
  }
  bool VisitCXXRecordDecl(CXXRecordDecl *RD) {
    if (RD->isThisDeclarationADefinition() && !RD->isDependentContext())
      Records.push_back(RD);
    return true;
  }
  bool VisitLambdaExpr(LambdaExpr *L) {
    if (!InstantiationDepth)
      Lambdas.push_back(L);
    return true;
  }

  std::vector<FunctionDecl *> Functions;
  llvm::SetVector<FunctionDecl *> Declared;
  std::vector<CallExpr *> Calls;
  std::vector<DeclRefExpr *> Refs;
  std::vector<MemberExpr *> Members;
  std::vector<LambdaExpr *> Lambdas;
  std::vector<CXXRecordDecl *> Records;
  llvm::DenseSet<DeclarationName> UnresolvedFree;
  llvm::DenseSet<DeclarationName> UnresolvedMethods;
  llvm::DenseSet<const FunctionDecl *> UnresolvedFound;
  llvm::DenseSet<const FunctionDecl *> UsedInInstantiation;

private:
  void addFound(OverloadExpr *E) {
    for (NamedDecl *D : E->decls())
      if (auto *FD = dyn_cast<FunctionDecl>(D->getUnderlyingDecl()))
        UnresolvedFound.insert(FD->getCanonicalDecl());
  }

  static bool isInstantiation(const Decl *D) {
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      return FD->isTemplateInstantiation();
    if (const auto *CTS = dyn_cast<ClassTemplateSpecializationDecl>(D))
      return isTemplateInstantiation(CTS->getSpecializationKind());
    if (const auto *VTS = dyn_cast<VarTemplateSpecializationDecl>(D))
      return isTemplateInstantiation(VTS->getSpecializationKind());
    return false;
  }

  unsigned InstantiationDepth = 0;
};

struct Candidate {
  FunctionDecl *FD;
  /// Null for a candidate about the implicit object parameter, `this`.
  ParmVarDecl *Param;
  unsigned Index;
  /// Which rewrite proposed it (Rewrite::id).
  unsigned Rewrite;
  /// Edits fixed when the candidate is proposed.
  llvm::SmallVector<FixItHint> Edits;
  /// Edits recomputed by Rewrite::finalize, which can depend on which other
  /// candidates are still live (a call site passing a parameter that is
  /// rewritten too needs no edit).
  llvm::SmallVector<FixItHint> LateEdits;
  /// Candidates of the same rewrite this one is only right with.
  llvm::SmallVector<const ParmVarDecl *> Needs;
  /// Like Needs, but found by Rewrite::finalize.
  llvm::SmallVector<const ParmVarDecl *> LateNeeds;
  /// Project mode: the function is defined in another translation unit, and
  /// this one only edits its declarations and calls.
  bool External = false;
  bool Live = true;
  StringRef Why;
};

inline CharSourceRange chars(SourceLocation Loc, unsigned Length) {
  return CharSourceRange::getCharRange(Loc, Loc.getLocWithOffset(Length));
}

/// What every rewrite shares: the AST index, symbol names, the plan, and the
/// candidates proposed so far.
class Context {
public:
  Context(ASTContext &C, const CIRModernizeOptions &Options);

  ASTContext &C;
  SourceManager &SM;
  const CIRModernizeOptions &Options;
  Collector Visitor;
  std::vector<Candidate> Candidates;

  bool projectMode() const { return !Options.ExportPath.empty(); }

  /// A project-wide rewrite may edit any header but a system one; otherwise
  /// only the main file, the one place a single translation unit sees all of.
  bool editable(SourceLocation Loc) const;

  std::string mangle(const FunctionDecl *FD) { return mangle(GlobalDecl(FD)); }
  std::string mangle(GlobalDecl GD);
  std::string usr(const FunctionDecl *FD);

  /// The function CIR names Mangled, if this translation unit declares it.
  const FunctionDecl *declByMangledName(StringRef Mangled);

  /// Why FD cannot have its signature rewritten, or empty if it can.
  StringRef whyIneligible(FunctionDecl *FD);

  /// Why FD must keep a signature C can read (references are C++ only), or
  /// empty if only C++ sees it.
  StringRef whyVisibleToC(const FunctionDecl *FD) const;

  /// Parameter indices of FD that another translation unit proposed for
  /// Rewrite, or null.
  const llvm::DenseSet<unsigned> *planned(const FunctionDecl *FD,
                                          StringRef Rewrite);
  void plan(StringRef Rewrite, StringRef USR, unsigned Index) {
    Plan[Rewrite][USR].insert(Index);
  }

  /// Records a candidate; with a reason it is recorded as blocked (project
  /// mode, so the merge hears why) or dropped.
  void add(Candidate Cand, StringRef Why);
  bool live(const ParmVarDecl *P, unsigned Rewrite) const;

  /// Counts a reason for --why.
  void count(StringRef Why) { ++Drops[Why]; }
  llvm::StringMap<unsigned> &drops() { return Drops; }
  StringRef save(const llvm::Twine &S) { return Saver.save(S); }

  /// The parameter candidates are keyed by: the definition's when this
  /// translation unit has the body, otherwise the first declaration's.
  static ParmVarDecl *keyParam(const FunctionDecl *FD, unsigned Index);
  static unsigned thisOffset(const FunctionDecl *FD);
  static std::optional<PointerTypeLoc> starOf(const ParmVarDecl *P);

private:
  void indexCallees();

  std::unique_ptr<MangleContext> Mangler;
  llvm::DenseSet<const Expr *> CalleeRefs;
  bool CalleesIndexed = false;
  llvm::StringMap<const FunctionDecl *> ByMangled;
  bool MangledIndexed = false;
  llvm::DenseMap<std::pair<const ParmVarDecl *, unsigned>, size_t> ByParam;
  llvm::BumpPtrAllocator Alloc;
  llvm::StringSaver Saver{Alloc};
  llvm::StringMap<unsigned> Drops;
  llvm::StringMap<llvm::StringMap<llvm::DenseSet<unsigned>>> Plan;
};

/// One kind of rewrite. Facts come from CIR in analyze; candidates for the
/// functions this translation unit defines come from addDefined, and for
/// functions planned elsewhere from addExternal.
class Rewrite {
public:
  Rewrite(Context &Ctx, unsigned Id) : Ctx(Ctx), Id(Id) {}
  virtual ~Rewrite() = default;

  unsigned id() const { return Id; }
  /// Its name in -fcir-modernize-only, the plan and the export.
  virtual StringRef name() const = 0;
  /// The warning, with %0 the parameter and %1 the function.
  virtual unsigned diagID(DiagnosticsEngine &Diags) const = 0;
  /// Why a candidate is blocked when one it needs is.
  virtual StringRef whyNeedBlocked() const = 0;
  /// Whether the function's type changes, so code that names it may need an
  /// edit; an attribute that leaves the type alone needs none, even in an
  /// #if branch this build skips.
  virtual bool changesType() const { return true; }

  virtual void analyze(mlir::ModuleOp Module) = 0;
  virtual void addDefined(FunctionDecl *FD) = 0;
  virtual void addExternal(FunctionDecl *FD, unsigned Index) = 0;
  /// Called while settling which candidates survive, once all it needs are
  /// live: rebuild LateEdits and LateNeeds, or say why it cannot.
  virtual StringRef finalize(Candidate &Cand) { return {}; }

protected:
  Context &Ctx;
  const unsigned Id;
};

std::unique_ptr<Rewrite> createParamToReference(Context &Ctx, unsigned Id);
std::unique_ptr<Rewrite> createPointerToConst(Context &Ctx, unsigned Id);
std::unique_ptr<Rewrite> createNonNull(Context &Ctx, unsigned Id);
std::unique_ptr<Rewrite> createNullable(Context &Ctx, unsigned Id);
std::unique_ptr<Rewrite> createLifetimebound(Context &Ctx, unsigned Id);
std::unique_ptr<Rewrite> createLifetimeboundThis(Context &Ctx, unsigned Id);
std::unique_ptr<Rewrite> createCaptureBy(Context &Ctx, unsigned Id);

/// Writes the "sites" array of a project-mode export: every unsafe construct
/// in the translation unit's own source (Inventory.cpp).
void exportInventory(llvm::json::OStream &J, Context &Ctx);

} // namespace clang::cirmodernize

#endif // CLANG_LIB_CIR_FRONTENDACTION_CIRMODERNIZEFRAMEWORK_H
