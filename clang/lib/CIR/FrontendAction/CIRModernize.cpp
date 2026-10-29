//===--- CIRModernize.cpp - CIR-proven idiomatic C++ rewrites -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pointer parameter to reference. CIR supplies the per-parameter facts
// (must-dereference, never reassigned, address never taken, no arithmetic, no
// null evidence); the AST supplies every spelling that has to change: the
// declarator `*` in each redeclaration, `p->m` and `*p` in the body, and each
// call site argument. A candidate is dropped as a whole as soon as any use or
// call site falls outside the shapes handled here, and candidates that forward
// their parameter to another candidate stand or fall together.
//
//===----------------------------------------------------------------------===//

#include "CIRModernize.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CIR/Dialect/Analysis/CIRNullabilitySafety.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"

using namespace clang;

#define DEBUG_TYPE "cir-modernize"

namespace {

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
  bool VisitOverloadExpr(OverloadExpr *E) {
    Unresolved.insert(E->getName());
    return true;
  }
  bool VisitCXXDependentScopeMemberExpr(CXXDependentScopeMemberExpr *E) {
    Unresolved.insert(E->getMember());
    return true;
  }
  bool VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E) {
    Unresolved.insert(E->getDeclName());
    return true;
  }
  bool VisitLambdaExpr(LambdaExpr *L) {
    if (!InstantiationDepth)
      Lambdas.push_back(L);
    return true;
  }

  std::vector<FunctionDecl *> Functions;
  std::vector<CallExpr *> Calls;
  std::vector<DeclRefExpr *> Refs;
  std::vector<MemberExpr *> Members;
  std::vector<LambdaExpr *> Lambdas;
  llvm::DenseSet<DeclarationName> Unresolved;
  llvm::DenseSet<const FunctionDecl *> UsedInInstantiation;

private:
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
  ParmVarDecl *Param;
  unsigned Index;
  llvm::SmallVector<FixItHint> BodyEdits;
  llvm::SmallVector<FixItHint> CallEdits;
  llvm::SmallVector<const ParmVarDecl *> Needs;
  bool Live = true;
};

CharSourceRange chars(SourceLocation Loc, unsigned Length) {
  return CharSourceRange::getCharRange(Loc, Loc.getLocWithOffset(Length));
}

class Modernizer {
public:
  Modernizer(ASTContext &C, const CIRModernizeOptions &Options)
      : C(C), SM(C.getSourceManager()), Options(Options) {}

  void run(mlir::ModuleOp Module, DiagnosticsEngine &Diags) {
    Visitor.TraverseDecl(C.getTranslationUnitDecl());
    collectFacts(Module);
    indexCallees();
    for (FunctionDecl *FD : Visitor.Functions)
      addCandidates(FD);
    resolve();
    report(Diags);
    LLVM_DEBUG({
      Drops["rewritten"] = llvm::count_if(
          Candidates, [](const Candidate &Cand) { return Cand.Live; });
      for (const auto &[Why, N] : Drops)
        llvm::dbgs() << "cir-modernize: " << N << " " << Why << "\n";
    });
  }

private:
  using FactMap = llvm::DenseMap<unsigned, cir::NullabilityFact>;

  void collectFacts(mlir::ModuleOp Module) {
    cir::NullabilitySafetyCIROptions FactOptions;
    cir::collectNullabilityFacts(
        Module, FactOptions, [](mlir::Location) { return true; },
        [&](const cir::NullabilityFact &F) {
          if (F.kind == cir::NullabilityFactKind::Param)
            Facts[F.function].try_emplace(F.index, F);
        });
  }

  void indexCallees() {
    for (CallExpr *CE : Visitor.Calls) {
      const Expr *Callee = CE->getCallee()->IgnoreParenImpCasts();
      if (auto *DRE = dyn_cast<DeclRefExpr>(Callee))
        CalleeRefs.insert(DRE);
      else if (auto *ME = dyn_cast<MemberExpr>(Callee))
        CalleeRefs.insert(ME);
    }
  }

  std::string mangle(const FunctionDecl *FD) {
    if (!Mangler)
      Mangler.reset(C.createMangleContext());
    if (!Mangler->shouldMangleDeclName(FD))
      return FD->getName().str();
    std::string Name;
    llvm::raw_string_ostream OS(Name);
    Mangler->mangleName(GlobalDecl(FD), OS);
    return Name;
  }

  bool inMainFile(SourceLocation Loc) const {
    return Loc.isValid() && !Loc.isMacroID() && SM.isInMainFile(Loc);
  }

  // Why FD cannot have its signature rewritten, or empty if it can.
  StringRef whyIneligible(FunctionDecl *FD) {
    if (isa<CXXConstructorDecl, CXXDestructorDecl, CXXConversionDecl>(FD) ||
        FD->isOverloadedOperator() || FD->isMain() || FD->isVariadic() ||
        !FD->getIdentifier())
      return "special function";
    if (FD->isTemplated() || FD->isTemplateInstantiation() ||
        FD->getTemplatedKind() != FunctionDecl::TK_NonTemplate)
      return "template";
    if (isa_and_nonnull<CoroutineBodyStmt>(FD->getBody()))
      return "coroutine";
    if (auto *MD = dyn_cast<CXXMethodDecl>(FD))
      if (MD->isVirtual() || MD->size_overridden_methods() ||
          MD->getParent()->isLocalClass())
        return "virtual or local class method";
    if (FD->isExternallyVisible() && !Options.AllowPublic)
      return "externally visible";
    for (const FunctionDecl *R : FD->redecls())
      if (!inMainFile(R->getLocation()))
        return "declared outside the main file";
    if (Visitor.Unresolved.contains(FD->getDeclName()))
      return "name used in a dependent call";
    const FunctionDecl *Canon = FD->getCanonicalDecl();
    if (Visitor.UsedInInstantiation.contains(Canon))
      return "used from a template instantiation";
    for (const NamedDecl *ND :
         FD->getDeclContext()->getRedeclContext()->lookup(FD->getDeclName())) {
      auto *Other = dyn_cast<FunctionDecl>(ND);
      if (!Other || Other->getCanonicalDecl() != Canon)
        return "overloaded";
    }
    for (DeclRefExpr *DRE : Visitor.Refs)
      if (auto *Ref = dyn_cast<FunctionDecl>(DRE->getDecl()))
        if (Ref->getCanonicalDecl() == Canon && !CalleeRefs.contains(DRE))
          return "address taken";
    for (MemberExpr *ME : Visitor.Members)
      if (auto *Ref = dyn_cast<FunctionDecl>(ME->getMemberDecl()))
        if (Ref->getCanonicalDecl() == Canon && !CalleeRefs.contains(ME))
          return "address taken";
    return {};
  }

  // Why a must-dereference parameter still cannot become a reference, or
  // empty if the facts allow it.
  static StringRef whyFactIneligible(const cir::NullabilityFact &F) {
    if (F.reassigned)
      return "fact: reassigned";
    if (F.addressTaken)
      return "fact: address taken";
    if (F.arithmetic)
      return "fact: pointer arithmetic";
    if (F.nullChecked)
      return "fact: null checked";
    if (F.assignedNull)
      return "fact: assigned null";
    if (F.callersPassNull)
      return "fact: a caller passes null";
    if (F.declared == "nullable")
      return "fact: declared nullable";
    return {};
  }

  static std::optional<PointerTypeLoc> starOf(const ParmVarDecl *P) {
    TypeSourceInfo *TSI = P->getTypeSourceInfo();
    if (!TSI)
      return std::nullopt;
    auto PTL = TSI->getTypeLoc().getUnqualifiedLoc().getAs<PointerTypeLoc>();
    if (!PTL)
      return std::nullopt;
    return PTL;
  }

  bool paramEligible(const ParmVarDecl *P) {
    if (P->hasDefaultArg() || !P->getIdentifier())
      return false;
    const auto *PT = P->getType()->getAs<PointerType>();
    if (!PT)
      return false;
    QualType Pointee = PT->getPointeeType();
    if (Pointee->isVoidType() || Pointee->isFunctionType() ||
        Pointee->isPointerType() || Pointee->isArrayType())
      return false;
    for (const LambdaExpr *L : Visitor.Lambdas)
      for (const LambdaCapture &Capture : L->captures())
        if (Capture.capturesVariable() && Capture.getCapturedVar() == P)
          return false;
    return true;
  }

  DynTypedNode parentOf(const DynTypedNode &Node) {
    DynTypedNodeList Parents = C.getParents(Node);
    return Parents.empty() ? DynTypedNode() : Parents[0];
  }

  bool evaluatedInBody(const Expr *E, const FunctionDecl *FD) {
    DynTypedNode Node = DynTypedNode::create(*E);
    while (true) {
      Node = parentOf(Node);
      if (Node.get<TypeLoc>() || Node.get<LambdaExpr>() ||
          Node.get<BlockExpr>() || Node.get<UnaryExprOrTypeTraitExpr>() ||
          Node.get<CXXNoexceptExpr>() || Node.get<CXXTypeidExpr>())
        return false;
      if (const Decl *D = Node.get<Decl>()) {
        if (D == FD)
          return true;
        if (isa<FunctionDecl, RecordDecl, BlockDecl, CapturedDecl>(D))
          return false;
      }
      if (Node.getNodeKind().isNone())
        return false;
    }
  }

  const Stmt *useParent(const Expr *E, const Expr *&Child) {
    Child = E;
    while (true) {
      DynTypedNode P = parentOf(DynTypedNode::create(*Child));
      const auto *PE = P.get<Expr>();
      if (!PE)
        return P.get<Stmt>();
      if (isa<ParenExpr>(PE)) {
        Child = PE;
        continue;
      }
      if (const auto *ICE = dyn_cast<ImplicitCastExpr>(PE))
        if (ICE->getCastKind() == CK_LValueToRValue ||
            ICE->getCastKind() == CK_NoOp) {
          Child = PE;
          continue;
        }
      return PE;
    }
  }

  static std::optional<unsigned> argIndex(const CallExpr *CE, const Expr *E) {
    if (isa<CXXOperatorCallExpr>(CE))
      return std::nullopt;
    for (unsigned I = 0, N = CE->getNumArgs(); I != N; ++I)
      if (CE->getArg(I) == E)
        return I;
    return std::nullopt;
  }

  bool bodyEdits(Candidate &Cand) {
    for (DeclRefExpr *DRE : Visitor.Refs) {
      if (DRE->getDecl() != Cand.Param)
        continue;
      if (DRE->getLocation().isMacroID() || !evaluatedInBody(DRE, Cand.FD))
        return false;
      const Expr *Child;
      const Stmt *Parent = useParent(DRE, Child);
      if (const auto *ME = dyn_cast_or_null<MemberExpr>(Parent)) {
        if (!ME->isArrow() || ME->getBase() != Child ||
            ME->getOperatorLoc().isMacroID())
          return false;
        Cand.BodyEdits.push_back(
            FixItHint::CreateReplacement(chars(ME->getOperatorLoc(), 2), "."));
        continue;
      }
      if (const auto *UO = dyn_cast_or_null<UnaryOperator>(Parent)) {
        if (UO->getOpcode() != UO_Deref || UO->getOperatorLoc().isMacroID())
          return false;
        Cand.BodyEdits.push_back(
            FixItHint::CreateRemoval(chars(UO->getOperatorLoc(), 1)));
        continue;
      }
      if (const auto *CE = dyn_cast_or_null<CallExpr>(Parent)) {
        std::optional<unsigned> Index = argIndex(CE, Child);
        const FunctionDecl *Callee = CE->getDirectCallee();
        if (!Index || !Callee || !(Callee = Callee->getDefinition()) ||
            *Index >= Callee->getNumParams())
          return false;
        Cand.Needs.push_back(Callee->getParamDecl(*Index));
        continue;
      }
      return false;
    }
    return true;
  }

  void addCandidates(FunctionDecl *FD) {
    if (isa<CXXConstructorDecl, CXXDestructorDecl>(FD) ||
        FD->isDependentContext() || FD->isTemplated())
      return;
    auto It = Facts.find(mangle(FD));
    if (It == Facts.end())
      return;
    StringRef FunctionWhy = whyIneligible(FD);
    unsigned Offset =
        isa<CXXMethodDecl>(FD) && cast<CXXMethodDecl>(FD)->isInstance() ? 1 : 0;
    for (auto &[ArgIndex, Fact] : It->second) {
      if (ArgIndex < Offset || !Fact.mustDeref ||
          ArgIndex - Offset >= FD->getNumParams())
        continue;
      unsigned Index = ArgIndex - Offset;
      ++Drops["must-dereference parameters"];
      StringRef Why =
          FunctionWhy.empty() ? whyFactIneligible(Fact) : FunctionWhy;
      if (!Why.empty()) {
        ++Drops[Why];
        continue;
      }
      ParmVarDecl *P = FD->getParamDecl(Index);
      if (!paramEligible(P)) {
        ++Drops["parameter type, default argument or lambda capture"];
        continue;
      }
      Candidate Cand{FD, P, Index};
      bool Ok = true;
      for (const FunctionDecl *R : FD->redecls()) {
        std::optional<PointerTypeLoc> Star = starOf(R->getParamDecl(Index));
        if (!Star || Star->getStarLoc().isMacroID()) {
          Ok = false;
          break;
        }
        Cand.BodyEdits.push_back(
            FixItHint::CreateReplacement(chars(Star->getStarLoc(), 1), "&"));
      }
      if (!Ok) {
        ++Drops["declarator is not a plain pointer"];
      } else if (!bodyEdits(Cand)) {
        ++Drops["a use in the body has an unhandled shape"];
      } else {
        ByParam[P] = Candidates.size();
        Candidates.push_back(std::move(Cand));
      }
    }
  }

  bool live(const ParmVarDecl *P) const {
    auto It = ByParam.find(P);
    return It != ByParam.end() && Candidates[It->second].Live;
  }

  static bool simpleOperand(const Expr *E) {
    return isa<DeclRefExpr, MemberExpr, CallExpr, ArraySubscriptExpr, ParenExpr,
               CXXThisExpr>(E);
  }

  bool callEdits(Candidate &Cand) {
    Cand.CallEdits.clear();
    const FunctionDecl *Canon = Cand.FD->getCanonicalDecl();
    for (CallExpr *CE : Visitor.Calls) {
      const FunctionDecl *Callee = CE->getDirectCallee();
      if (!Callee || Callee->getCanonicalDecl() != Canon)
        continue;
      if (isa<CXXOperatorCallExpr>(CE) || Cand.Index >= CE->getNumArgs())
        return false;
      const Expr *Arg = CE->getArg(Cand.Index);
      SourceLocation Begin = Arg->getBeginLoc(), End = Arg->getEndLoc();
      if (Begin.isMacroID() || End.isMacroID() ||
          Arg->isNullPointerConstant(C, Expr::NPC_ValueDependentIsNotNull))
        return false;
      for (unsigned I = 0, N = CE->getNumArgs(); I != N; ++I)
        if (I != Cand.Index && CE->getArg(I)->HasSideEffects(C))
          return false;
      const Expr *Inner = Arg->IgnoreParenImpCasts();
      if (const auto *ICE = dyn_cast<ImplicitCastExpr>(Arg->IgnoreParens()))
        if (ICE->getCastKind() == CK_ArrayToPointerDecay)
          return false;
      if (const auto *DRE = dyn_cast<DeclRefExpr>(Inner))
        if (const auto *P = dyn_cast<ParmVarDecl>(DRE->getDecl()))
          if (live(P))
            continue;
      if (const auto *UO = dyn_cast<UnaryOperator>(Inner);
          UO && UO->getOpcode() == UO_AddrOf) {
        Cand.CallEdits.push_back(
            FixItHint::CreateRemoval(chars(UO->getOperatorLoc(), 1)));
        continue;
      }
      if (simpleOperand(Inner)) {
        Cand.CallEdits.push_back(FixItHint::CreateInsertion(Begin, "*"));
        continue;
      }
      Cand.CallEdits.push_back(FixItHint::CreateInsertion(Begin, "*("));
      Cand.CallEdits.push_back(FixItHint::CreateInsertion(
          Lexer::getLocForEndOfToken(End, 0, SM, C.getLangOpts()), ")"));
    }
    return true;
  }

  void resolve() {
    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (Candidate &Cand : Candidates) {
        if (!Cand.Live)
          continue;
        StringRef Why;
        if (!llvm::all_of(Cand.Needs,
                          [&](const ParmVarDecl *P) { return live(P); }))
          Why = "forwarded to a parameter that stays a pointer";
        else if (!callEdits(Cand))
          Why = "a call site has an unhandled shape";
        if (!Why.empty()) {
          ++Drops[Why];
          Cand.Live = false;
          Changed = true;
        }
      }
    }
  }

  void report(DiagnosticsEngine &Diags) {
    unsigned ID = Diags.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "pointer parameter %0 of %1 is dereferenced on every path, so a null "
        "argument is already undefined behavior; it can be a reference");
    Rewriter R(SM, C.getLangOpts());
    for (const Candidate &Cand : Candidates) {
      if (!Cand.Live)
        continue;
      auto Diag = Diags.Report(Cand.Param->getLocation(), ID);
      Diag << Cand.Param << Cand.FD;
      for (const FixItHint &Hint : Cand.BodyEdits)
        Diag << Hint;
      for (const FixItHint &Hint : Cand.CallEdits)
        Diag << Hint;
      if (!Options.Apply)
        continue;
      for (const auto *Edits : {&Cand.BodyEdits, &Cand.CallEdits})
        for (const FixItHint &Hint : *Edits) {
          SourceLocation Begin = Hint.RemoveRange.getBegin();
          if (Begin == Hint.RemoveRange.getEnd())
            R.InsertTextAfter(Begin, Hint.CodeToInsert);
          else
            R.ReplaceText(Hint.RemoveRange, Hint.CodeToInsert);
        }
    }
    if (Options.Apply)
      R.overwriteChangedFiles();
  }

  ASTContext &C;
  SourceManager &SM;
  const CIRModernizeOptions &Options;
  Collector Visitor;
  std::unique_ptr<MangleContext> Mangler;
  llvm::StringMap<FactMap> Facts;
  llvm::DenseSet<const Expr *> CalleeRefs;
  std::vector<Candidate> Candidates;
  llvm::DenseMap<const ParmVarDecl *, size_t> ByParam;
  // Must-dereference parameters by the first reason they were not rewritten.
  llvm::StringMap<unsigned> Drops;
};

} // namespace

void clang::runCIRModernize(mlir::ModuleOp Module, ASTContext &C,
                            DiagnosticsEngine &Diags,
                            const CIRModernizeOptions &Options) {
  Modernizer(C, Options).run(Module, Diags);
}
