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
#include "clang/CIR/Dialect/Analysis/CIRPointeeWrites.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/UnifiedSymbolResolution/USRGeneration.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/IOSandbox.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"

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

enum class RewriteKind : unsigned {
  // Pointer parameter to reference.
  Reference,
  // Pointer parameter to pointer to const.
  Const,
};

StringRef kindName(RewriteKind K) {
  return K == RewriteKind::Reference ? "ref" : "const";
}

struct Candidate {
  FunctionDecl *FD;
  ParmVarDecl *Param;
  unsigned Index;
  RewriteKind Kind = RewriteKind::Reference;
  llvm::SmallVector<FixItHint> BodyEdits;
  llvm::SmallVector<FixItHint> CallEdits;
  llvm::SmallVector<const ParmVarDecl *> Needs;
  // Parameters passed straight through at a call site, so no edit was made
  // there; the call is only right while they become references too.
  llvm::SmallVector<const ParmVarDecl *> CallNeeds;
  // Project mode: the function is defined in another translation unit, and
  // this one only edits its declarations and calls.
  bool External = false;
  bool Live = true;
  StringRef Why;
};

CharSourceRange chars(SourceLocation Loc, unsigned Length) {
  return CharSourceRange::getCharRange(Loc, Loc.getLocWithOffset(Length));
}

class Modernizer {
public:
  Modernizer(ASTContext &C, const CIRModernizeOptions &Options)
      : C(C), SM(C.getSourceManager()), Options(Options) {}

  void run(mlir::ModuleOp Module, DiagnosticsEngine &Diags) {
    if (!Options.PlanPath.empty() && !loadPlan(Diags))
      return;
    Visitor.TraverseDecl(C.getTranslationUnitDecl());
    collectFacts(Module);
    if (enabled("const"))
      collectConstFacts(Module);
    indexCallees();
    bool Ref = enabled("ref"), Const = enabled("const");
    for (FunctionDecl *FD : Visitor.Functions) {
      if (Ref)
        addCandidates(FD);
      if (Const)
        addConstCandidates(FD);
    }
    if (Ref)
      addExternalCandidates();
    if (Const)
      addExternalConstCandidates();
    resolve();
    if (projectMode())
      exportCandidates(Diags);
    else
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

  std::string mangle(const FunctionDecl *FD) { return mangle(GlobalDecl(FD)); }

  std::string mangle(GlobalDecl GD) {
    if (!Mangler)
      Mangler.reset(C.createMangleContext());
    const auto *ND = cast<NamedDecl>(GD.getDecl());
    if (!Mangler->shouldMangleDeclName(ND))
      return ND->getName().str();
    std::string Name;
    llvm::raw_string_ostream OS(Name);
    Mangler->mangleName(GD, OS);
    return Name;
  }

  bool projectMode() const { return !Options.ExportPath.empty(); }

  bool enabled(StringRef Rewrite) const {
    if (Options.Only.empty())
      return true;
    llvm::SmallVector<StringRef> Names;
    StringRef(Options.Only).split(Names, ',');
    return llvm::is_contained(Names, Rewrite);
  }

  // A project-wide rewrite may edit any header but a system one; otherwise
  // only the main file, the one place a single translation unit sees all of.
  bool editable(SourceLocation Loc) const {
    if (Loc.isInvalid() || Loc.isMacroID())
      return false;
    return projectMode() ? !SM.isInSystemHeader(Loc) : SM.isInMainFile(Loc);
  }

  std::string usr(const FunctionDecl *FD) {
    llvm::SmallString<128> Buf;
    if (index::generateUSRForDecl(FD->getCanonicalDecl(), Buf))
      return {};
    return std::string(Buf);
  }

  const llvm::DenseSet<unsigned> *
  planned(const FunctionDecl *FD,
          RewriteKind Kind = RewriteKind::Reference) {
    auto &P = Plan[static_cast<unsigned>(Kind)];
    if (P.empty())
      return nullptr;
    auto It = P.find(usr(FD));
    return It == P.end() ? nullptr : &It->second;
  }

  // The parameter candidates are keyed by: the definition's when this
  // translation unit has the body, otherwise the first declaration's.
  static ParmVarDecl *keyParam(const FunctionDecl *FD, unsigned Index) {
    const FunctionDecl *Def = FD->getDefinition();
    const FunctionDecl *Key =
        Def && Def->hasBody() ? Def : FD->getCanonicalDecl();
    return Index < Key->getNumParams()
               ? const_cast<ParmVarDecl *>(Key->getParamDecl(Index))
               : nullptr;
  }

  static unsigned thisOffset(const FunctionDecl *FD) {
    const auto *MD = dyn_cast<CXXMethodDecl>(FD);
    return MD && MD->isInstance() ? 1 : 0;
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
    if (FD->isExternallyVisible() && !Options.AllowPublic && !projectMode())
      return "externally visible";
    for (const FunctionDecl *R : FD->redecls())
      if (!editable(R->getLocation()))
        return projectMode() ? "declared in a system header or a macro"
                             : "declared outside the main file";
    if (projectMode() && usr(FD).empty())
      return "no USR";
    if (Visitor.UnresolvedFound.contains(FD->getCanonicalDecl()) ||
        (isa<CXXMethodDecl>(FD) ? Visitor.UnresolvedMethods
                                : Visitor.UnresolvedFree)
            .contains(FD->getDeclName()))
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

  // Why P cannot become a reference whatever the facts say, or empty.
  StringRef whyParamIneligible(const ParmVarDecl *P) {
    if (P->hasDefaultArg())
      return "default argument";
    if (!P->getIdentifier())
      return "unnamed parameter";
    const auto *PT = P->getType()->getAs<PointerType>();
    if (!PT)
      return "not a plain pointer";
    QualType Pointee = PT->getPointeeType();
    if (Pointee->isVoidType())
      return "pointer to void";
    if (Pointee->isFunctionType())
      return "function pointer";
    if (Pointee->isPointerType())
      return "pointer to pointer";
    if (Pointee->isArrayType())
      return "pointer to array";
    for (const LambdaExpr *L : Visitor.Lambdas)
      for (const LambdaCapture &Capture : L->captures())
        if (Capture.capturesVariable() && Capture.getCapturedVar() == P)
          return "captured by a lambda";
    return {};
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
        ParmVarDecl *Needed =
            Index && Callee ? keyParam(Callee, *Index) : nullptr;
        // Without a body here the callee can still be a candidate in project
        // mode, defined in another translation unit.
        if (!Needed || (!projectMode() && !Callee->getDefinition()))
          return false;
        Cand.Needs.push_back(Needed);
        continue;
      }
      return false;
    }
    return true;
  }

  // The declarator `*` of every declaration this translation unit sees.
  bool declEdits(Candidate &Cand) {
    for (const FunctionDecl *R : Cand.FD->redecls()) {
      const ParmVarDecl *RP = R->getParamDecl(Cand.Index);
      std::optional<PointerTypeLoc> Star = starOf(RP);
      if (RP->hasDefaultArg() || !Star || Star->getStarLoc().isMacroID())
        return false;
      Cand.BodyEdits.push_back(
          FixItHint::CreateReplacement(chars(Star->getStarLoc(), 1), "&"));
    }
    return true;
  }

  void addCandidate(Candidate Cand, StringRef Why) {
    if (!Why.empty()) {
      ++Drops[Why];
      // A project-wide rewrite needs to hear why this translation unit
      // blocks a function, not just that it found no candidate.
      if (!projectMode())
        return;
      Cand.Live = false;
      Cand.Why = Why;
    }
    ByParam[{Cand.Param, static_cast<unsigned>(Cand.Kind)}] = Candidates.size();
    Candidates.push_back(std::move(Cand));
  }

  // `const ` before the pointee of every declaration this translation unit
  // sees. Calls need no change: T * converts to const T *.
  bool constDeclEdits(Candidate &Cand) {
    for (const FunctionDecl *R : Cand.FD->redecls()) {
      std::optional<PointerTypeLoc> Star = starOf(R->getParamDecl(Cand.Index));
      if (!Star || Star->getStarLoc().isMacroID())
        return false;
      SourceLocation Begin = Star->getPointeeLoc().getBeginLoc();
      if (Begin.isInvalid() || Begin.isMacroID())
        return false;
      Cand.BodyEdits.push_back(FixItHint::CreateInsertion(Begin, "const "));
    }
    return true;
  }

  static StringRef whyConstParamIneligible(const ParmVarDecl *P) {
    const auto *PT = P->getType()->getAs<PointerType>();
    if (!PT)
      return "const: not a plain pointer";
    QualType Pointee = PT->getPointeeType();
    if (Pointee.isConstQualified())
      return "const: already const";
    if (Pointee->isFunctionType() || Pointee->isArrayType())
      return "const: pointer to function or array";
    return {};
  }

  // Whether a callee only reads through its CIR argument Index. CIR keeps no
  // const qualifiers, so the answer comes from the callee's declaration.
  cir::PointerArgUse argUse(cir::FuncOp Callee, unsigned Index) {
    const FunctionDecl *FD = ByMangled.lookup(Callee.getSymName());
    if (!FD)
      return cir::PointerArgUse::Writes;
    unsigned Offset = thisOffset(FD);
    if (Index < Offset)
      return cast<CXXMethodDecl>(FD)->isConst() ? cir::PointerArgUse::ReadOnly
                                                : cir::PointerArgUse::Writes;
    // Past the named parameters is a variadic argument, which %n can write.
    if (Index - Offset >= FD->getNumParams())
      return cir::PointerArgUse::Writes;
    QualType T = FD->getParamDecl(Index - Offset)->getType();
    if (!T->isPointerType() && !T->isReferenceType())
      return cir::PointerArgUse::Writes;
    if (T->getPointeeType().isConstQualified())
      return cir::PointerArgUse::ReadOnly;
    return T->isPointerType() ? cir::PointerArgUse::Forward
                              : cir::PointerArgUse::Writes;
  }

  void collectConstFacts(mlir::ModuleOp Module) {
    for (FunctionDecl *FD : Visitor.Declared) {
      // Only what CIR can call is mangled: deduction guides have no symbol,
      // and the mangler does not handle them.
      if (isa<CXXDestructorDecl, CXXDeductionGuideDecl>(FD) ||
          FD->isDependentContext() || FD->getType()->isDependentType())
        continue;
      if (auto *CD = dyn_cast<CXXConstructorDecl>(FD)) {
        for (CXXCtorType T : {Ctor_Complete, Ctor_Base})
          ByMangled[mangle(GlobalDecl(CD, T))] = CD;
        continue;
      }
      ByMangled[mangle(GlobalDecl(FD))] = FD;
    }
    cir::collectPointeeWrites(
        Module,
        [&](cir::FuncOp Callee, unsigned Index) {
          return argUse(Callee, Index);
        },
        [&](const cir::PointeeWriteFact &F) {
          ConstFacts[F.function].try_emplace(F.index, F);
        });
  }

  void addConstCandidates(FunctionDecl *FD) {
    if (isa<CXXConstructorDecl, CXXDestructorDecl>(FD) ||
        FD->isDependentContext() || FD->isTemplated())
      return;
    auto It = ConstFacts.find(mangle(FD));
    const llvm::DenseSet<unsigned> *Planned = planned(FD, RewriteKind::Const);
    if (It == ConstFacts.end() && !Planned)
      return;
    StringRef FunctionWhy = whyIneligible(FD);
    unsigned Offset = thisOffset(FD);
    llvm::SmallVector<unsigned> Indices;
    if (It != ConstFacts.end())
      for (auto &[ArgIndex, Fact] : It->second)
        if (ArgIndex >= Offset && ArgIndex - Offset < FD->getNumParams() &&
            FD->getParamDecl(ArgIndex - Offset)->getType()->isPointerType())
          Indices.push_back(ArgIndex - Offset);
    if (Planned)
      for (unsigned Index : *Planned)
        if (Index < FD->getNumParams() && !llvm::is_contained(Indices, Index))
          Indices.push_back(Index);
    llvm::sort(Indices);
    for (unsigned Index : Indices) {
      ParmVarDecl *P = FD->getParamDecl(Index);
      StringRef Why = whyConstParamIneligible(P);
      if (Why == "const: already const")
        continue;
      ++Drops["const: pointer parameters"];
      if (Why.empty())
        Why = FunctionWhy;
      Candidate Cand{FD, P, Index, RewriteKind::Const};
      if (Why.empty() && It != ConstFacts.end())
        if (auto Fact = It->second.find(Index + Offset);
            Fact != It->second.end()) {
          if (Fact->second.written)
            Why = Saver.save("const: " + Fact->second.why);
          for (auto &[Callee, CalleeIndex] : Fact->second.forwards) {
            const FunctionDecl *CFD = ByMangled.lookup(Callee);
            unsigned CalleeOffset = CFD ? thisOffset(CFD) : 0;
            ParmVarDecl *Needed =
                CFD && CalleeIndex >= CalleeOffset
                    ? keyParam(CFD, CalleeIndex - CalleeOffset)
                    : nullptr;
            if (!Needed && Why.empty())
              Why = "const: forwarded to an unknown function";
            if (Needed)
              Cand.Needs.push_back(Needed);
          }
        }
      if (Why.empty() && !constDeclEdits(Cand))
        Why = "const: pointer spelled through a typedef or macro";
      addCandidate(std::move(Cand), Why);
    }
  }

  void addExternalConstCandidates() {
    for (FunctionDecl *FD : Visitor.Declared) {
      if (FD->getDefinition() && FD->getDefinition()->hasBody())
        continue;
      const llvm::DenseSet<unsigned> *Planned =
          planned(FD, RewriteKind::Const);
      if (!Planned)
        continue;
      StringRef FunctionWhy = whyIneligible(FD);
      for (unsigned Index : *Planned) {
        if (Index >= FD->getNumParams())
          continue;
        ParmVarDecl *P = FD->getParamDecl(Index);
        StringRef Why = whyConstParamIneligible(P);
        if (Why.empty())
          Why = FunctionWhy;
        Candidate Cand{FD, P, Index, RewriteKind::Const};
        Cand.External = true;
        if (Why.empty() && !constDeclEdits(Cand))
          Why = "const: pointer spelled through a typedef or macro";
        addCandidate(std::move(Cand), Why);
      }
    }
  }

  void addCandidates(FunctionDecl *FD) {
    if (isa<CXXConstructorDecl, CXXDestructorDecl>(FD) ||
        FD->isDependentContext() || FD->isTemplated())
      return;
    auto It = Facts.find(mangle(FD));
    const llvm::DenseSet<unsigned> *Planned = planned(FD);
    if (It == Facts.end() && !Planned)
      return;
    StringRef FunctionWhy = whyIneligible(FD);
    unsigned Offset =
        isa<CXXMethodDecl>(FD) && cast<CXXMethodDecl>(FD)->isInstance() ? 1 : 0;
    // Parameters proven here, plus planned ones another translation unit
    // proved: an inline function in a header is emitted, and so analyzed,
    // only where it is used, but its body is the same everywhere.
    llvm::SmallVector<unsigned> Indices;
    if (It != Facts.end())
      for (auto &[ArgIndex, Fact] : It->second)
        // CIR models a reference parameter as a pointer too; it is already
        // what this rewrite would make it.
        if (ArgIndex >= Offset && Fact.mustDeref &&
            ArgIndex - Offset < FD->getNumParams() &&
            !FD->getParamDecl(ArgIndex - Offset)->getType()->isReferenceType())
          Indices.push_back(ArgIndex - Offset);
    if (Planned)
      for (unsigned Index : *Planned)
        if (Index < FD->getNumParams() && !llvm::is_contained(Indices, Index))
          Indices.push_back(Index);
    llvm::sort(Indices);
    for (unsigned Index : Indices) {
      ++Drops["must-dereference parameters"];
      StringRef Why = FunctionWhy;
      if (Why.empty() && It != Facts.end())
        if (auto Fact = It->second.find(Index + Offset);
            Fact != It->second.end())
          Why = Fact->second.mustDeref
                    ? whyFactIneligible(Fact->second)
                    : "fact: not dereferenced on every path in this "
                      "translation unit";
      ParmVarDecl *P = FD->getParamDecl(Index);
      if (Why.empty())
        Why = whyParamIneligible(P);
      Candidate Cand{FD, P, Index};
      if (Why.empty() && !declEdits(Cand))
        Why = "declarator is not a plain pointer";
      if (Why.empty() && !bodyEdits(Cand))
        Why = "a use in the body has an unhandled shape";
      addCandidate(std::move(Cand), Why);
    }
  }

  // Planned functions this translation unit declares or calls but does not
  // define: their declarations and calls here have to change with them.
  void addExternalCandidates() {
    for (FunctionDecl *FD : Visitor.Declared) {
      if (FD->getDefinition() && FD->getDefinition()->hasBody())
        continue;
      const llvm::DenseSet<unsigned> *Planned = planned(FD);
      if (!Planned)
        continue;
      StringRef FunctionWhy = whyIneligible(FD);
      for (unsigned Index : *Planned) {
        if (Index >= FD->getNumParams())
          continue;
        StringRef Why = FunctionWhy;
        ParmVarDecl *P = FD->getParamDecl(Index);
        if (Why.empty())
          Why = whyParamIneligible(P);
        Candidate Cand{FD, P, Index};
        Cand.External = true;
        if (Why.empty() && !declEdits(Cand))
          Why = "declarator is not a plain pointer";
        addCandidate(std::move(Cand), Why);
      }
    }
  }

  bool live(const ParmVarDecl *P,
            RewriteKind Kind = RewriteKind::Reference) const {
    auto It = ByParam.find({P, static_cast<unsigned>(Kind)});
    return It != ByParam.end() && Candidates[It->second].Live;
  }

  static bool simpleOperand(const Expr *E) {
    return isa<DeclRefExpr, MemberExpr, CallExpr, ArraySubscriptExpr, ParenExpr,
               CXXThisExpr>(E);
  }

  bool callEdits(Candidate &Cand) {
    Cand.CallEdits.clear();
    Cand.CallNeeds.clear();
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
          if (live(P)) {
            Cand.CallNeeds.push_back(P);
            continue;
          }
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
        if (!llvm::all_of(Cand.Needs, [&](const ParmVarDecl *P) {
              return live(P, Cand.Kind);
            }))
          Why = Cand.Kind == RewriteKind::Const
                    ? "const: forwarded to a parameter that stays non-const"
                    : "forwarded to a parameter that stays a pointer";
        else if (Cand.Kind == RewriteKind::Reference && !callEdits(Cand))
          Why = "a call site has an unhandled shape";
        if (!Why.empty()) {
          ++Drops[Why];
          Cand.Live = false;
          Cand.Why = Why;
          Changed = true;
        }
      }
    }
  }

  void report(DiagnosticsEngine &Diags) {
    unsigned RefID = Diags.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "pointer parameter %0 of %1 is dereferenced on every path, so a null "
        "argument is already undefined behavior; it can be a reference");
    unsigned ConstID = Diags.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "pointer parameter %0 of %1 is never written through; it can point "
        "to const");
    Rewriter R(SM, C.getLangOpts());
    for (const Candidate &Cand : Candidates) {
      if (!Cand.Live)
        continue;
      auto Diag = Diags.Report(Cand.Param->getLocation(),
                               Cand.Kind == RewriteKind::Const ? ConstID
                                                               : RefID);
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

  // {"functions": [{"usr": "...", "index": N}, ...]}
  bool loadPlan(DiagnosticsEngine &Diags) {
    auto Fail = [&](StringRef Why) {
      Diags.Report(Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                         "cannot read modernize plan %0: %1"))
          << Options.PlanPath << Why;
      return false;
    };
    // The plan and the export are side files of a project-wide rewrite, not
    // compiler inputs or outputs, like the files -fcir-modernize-apply writes.
    auto BypassSandbox = llvm::sys::sandbox::scopedDisable();
    auto Buf = llvm::MemoryBuffer::getFile(Options.PlanPath);
    if (!Buf)
      return Fail(Buf.getError().message());
    llvm::Expected<llvm::json::Value> V =
        llvm::json::parse((*Buf)->getBuffer());
    if (!V)
      return Fail(llvm::toString(V.takeError()));
    const llvm::json::Object *O = V->getAsObject();
    const llvm::json::Array *Fns = O ? O->getArray("functions") : nullptr;
    if (!Fns)
      return Fail("no functions array");
    for (const llvm::json::Value &Fn : *Fns) {
      const llvm::json::Object *E = Fn.getAsObject();
      std::optional<StringRef> USR = E ? E->getString("usr") : std::nullopt;
      std::optional<int64_t> Index = E ? E->getInteger("index") : std::nullopt;
      std::optional<StringRef> Kind = E ? E->getString("kind") : std::nullopt;
      if (!USR || !Index || *Index < 0 ||
          (Kind && *Kind != "ref" && *Kind != "const"))
        return Fail("bad function entry");
      RewriteKind K = Kind && *Kind == "const" ? RewriteKind::Const
                                               : RewriteKind::Reference;
      Plan[static_cast<unsigned>(K)][*USR].insert(*Index);
    }
    return true;
  }

  void exportEdit(llvm::json::OStream &J, const FixItHint &Hint) {
    auto [FID, Begin] = SM.getDecomposedLoc(Hint.RemoveRange.getBegin());
    unsigned End = SM.getFileOffset(Hint.RemoveRange.getEnd());
    OptionalFileEntryRef FE = SM.getFileEntryRefForID(FID);
    StringRef Path = FE ? FE->getFileEntry().tryGetRealPathName() : "";
    J.object([&] {
      J.attribute("file", Path.empty() && FE ? FE->getName() : Path);
      J.attribute("offset", Begin);
      J.attribute("length", End - Begin);
      J.attribute("text", Hint.CodeToInsert);
    });
  }

  void exportCandidates(DiagnosticsEngine &Diags) {
    auto BypassSandbox = llvm::sys::sandbox::scopedDisable();
    std::error_code EC;
    llvm::raw_fd_ostream OS(Options.ExportPath, EC);
    if (EC) {
      Diags.Report(Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                         "cannot write %0: %1"))
          << Options.ExportPath << EC.message();
      return;
    }
    llvm::json::OStream J(OS, 1);
    J.object([&] {
      J.attributeArray("candidates", [&] {
        for (const Candidate &Cand : Candidates)
          J.object([&] {
            J.attribute("usr", usr(Cand.FD));
            J.attribute("index", Cand.Index);
            J.attribute("kind", kindName(Cand.Kind));
            J.attribute("function", Cand.FD->getQualifiedNameAsString());
            J.attribute("param", Cand.Param->getName());
            J.attribute("defined", !Cand.External);
            J.attribute("location",
                        Cand.Param->getLocation().printToString(SM));
            if (!Cand.Live) {
              J.attribute("blocked", Cand.Why);
              return;
            }
            J.attributeArray("edits", [&] {
              for (const auto *Edits : {&Cand.BodyEdits, &Cand.CallEdits})
                for (const FixItHint &Hint : *Edits)
                  exportEdit(J, Hint);
            });
            J.attributeArray("needs", [&] {
              for (const auto *Needs : {&Cand.Needs, &Cand.CallNeeds})
                for (const ParmVarDecl *P : *Needs)
                  J.object([&] {
                    J.attribute("usr",
                                usr(cast<FunctionDecl>(P->getDeclContext())));
                    J.attribute("index", P->getFunctionScopeIndex());
                    J.attribute("kind", kindName(Cand.Kind));
                  });
            });
          });
      });
    });
  }

  ASTContext &C;
  SourceManager &SM;
  const CIRModernizeOptions &Options;
  Collector Visitor;
  std::unique_ptr<MangleContext> Mangler;
  llvm::StringMap<FactMap> Facts;
  llvm::DenseSet<const Expr *> CalleeRefs;
  std::vector<Candidate> Candidates;
  llvm::DenseMap<std::pair<const ParmVarDecl *, unsigned>, size_t> ByParam;
  llvm::StringMap<llvm::DenseMap<unsigned, cir::PointeeWriteFact>> ConstFacts;
  llvm::StringMap<const FunctionDecl *> ByMangled;
  llvm::BumpPtrAllocator Alloc;
  llvm::StringSaver Saver{Alloc};
  // Must-dereference parameters by the first reason they were not rewritten.
  llvm::StringMap<unsigned> Drops;
  // Indexed by RewriteKind.
  llvm::StringMap<llvm::DenseSet<unsigned>> Plan[2];
};

} // namespace

void clang::runCIRModernize(mlir::ModuleOp Module, ASTContext &C,
                            DiagnosticsEngine &Diags,
                            const CIRModernizeOptions &Options) {
  Modernizer(C, Options).run(Module, Diags);
}
