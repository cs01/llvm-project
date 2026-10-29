//===--- ParamToReference.cpp - Pointer parameter to reference ------------===//
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

#include "CIRModernizeFramework.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/CIR/Dialect/Analysis/CIRNullabilitySafety.h"
#include "clang/Lex/Lexer.h"

using namespace clang;
using namespace clang::cirmodernize;

namespace {

class ParamToReference : public Rewrite {
public:
  using Rewrite::Rewrite;

  StringRef name() const override { return "ref"; }
  unsigned diagID(DiagnosticsEngine &Diags) const override {
    return Diags.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "pointer parameter %0 of %1 is dereferenced on every path, so a "
        "null argument is already undefined behavior; it can be a "
        "reference");
  }
  StringRef whyNeedBlocked() const override {
    return "forwarded to a parameter that stays a pointer";
  }

  void analyze(mlir::ModuleOp Module) override {
    cir::NullabilitySafetyCIROptions FactOptions;
    cir::collectNullabilityFacts(
        Module, FactOptions, [](mlir::Location) { return true; },
        [&](const cir::NullabilityFact &F) {
          if (F.kind == cir::NullabilityFactKind::Param)
            Facts[F.function].try_emplace(F.index, F);
        });
  }

  void addDefined(FunctionDecl *FD) override {
    if (isa<CXXConstructorDecl, CXXDestructorDecl>(FD) ||
        FD->isDependentContext() || FD->isTemplated())
      return;
    auto It = Facts.find(Ctx.mangle(FD));
    const llvm::DenseSet<unsigned> *Planned = Ctx.planned(FD, name());
    if (It == Facts.end() && !Planned)
      return;
    StringRef FunctionWhy = Ctx.whyIneligible(FD);
    if (FunctionWhy.empty())
      FunctionWhy = Ctx.whyVisibleToC(FD);
    unsigned Offset = Context::thisOffset(FD);
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
      Ctx.count("must-dereference parameters");
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
      Candidate Cand{FD, P, Index, Id};
      if (Why.empty() && !declEdits(Cand))
        Why = "declarator is not a plain pointer";
      if (Why.empty() && !bodyEdits(Cand))
        Why = "a use in the body has an unhandled shape";
      Ctx.add(std::move(Cand), Why);
    }
  }

  void addExternal(FunctionDecl *FD, unsigned Index) override {
    StringRef Why = Ctx.whyIneligible(FD);
    ParmVarDecl *P = FD->getParamDecl(Index);
    if (Why.empty())
      Why = Ctx.whyVisibleToC(FD);
    if (Why.empty())
      Why = whyParamIneligible(P);
    Candidate Cand{FD, P, Index, Id};
    Cand.External = true;
    if (Why.empty() && !declEdits(Cand))
      Why = "declarator is not a plain pointer";
    Ctx.add(std::move(Cand), Why);
  }

  StringRef finalize(Candidate &Cand) override {
    return callEdits(Cand) ? StringRef() : "a call site has an unhandled shape";
  }

private:
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
    for (const LambdaExpr *L : Ctx.Visitor.Lambdas)
      for (const LambdaCapture &Capture : L->captures())
        if (Capture.capturesVariable() && Capture.getCapturedVar() == P)
          return "captured by a lambda";
    return {};
  }

  DynTypedNode parentOf(const DynTypedNode &Node) {
    DynTypedNodeList Parents = Ctx.C.getParents(Node);
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
    for (DeclRefExpr *DRE : Ctx.Visitor.Refs) {
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
        Cand.Edits.push_back(
            FixItHint::CreateReplacement(chars(ME->getOperatorLoc(), 2), "."));
        continue;
      }
      if (const auto *UO = dyn_cast_or_null<UnaryOperator>(Parent)) {
        if (UO->getOpcode() != UO_Deref || UO->getOperatorLoc().isMacroID())
          return false;
        Cand.Edits.push_back(
            FixItHint::CreateRemoval(chars(UO->getOperatorLoc(), 1)));
        continue;
      }
      if (const auto *CE = dyn_cast_or_null<CallExpr>(Parent)) {
        std::optional<unsigned> Index = argIndex(CE, Child);
        const FunctionDecl *Callee = CE->getDirectCallee();
        ParmVarDecl *Needed =
            Index && Callee ? Context::keyParam(Callee, *Index) : nullptr;
        // Without a body here the callee can still be a candidate in project
        // mode, defined in another translation unit.
        if (!Needed || (!Ctx.projectMode() && !Callee->getDefinition()))
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
      std::optional<PointerTypeLoc> Star = Context::starOf(RP);
      if (RP->hasDefaultArg() || !Star || Star->getStarLoc().isMacroID())
        return false;
      Cand.Edits.push_back(
          FixItHint::CreateReplacement(chars(Star->getStarLoc(), 1), "&"));
      // A reference takes no qualifier of its own: `T *const p` becomes
      // `T &p`. (The pointer was never reseated; a reference cannot be.)
      if (RP->getType().hasLocalQualifiers() &&
          !dropQualifiers(Cand, RP, *Star))
        return false;
    }
    return true;
  }

  // Removes the const, volatile and restrict written between the `*` and the
  // parameter's name.
  bool dropQualifiers(Candidate &Cand, const ParmVarDecl *P,
                      const PointerTypeLoc &Star) {
    const SourceManager &SM = Ctx.SM;
    const LangOptions &LO = Ctx.C.getLangOpts();
    SourceLocation From = Star.getStarLoc().getLocWithOffset(1);
    SourceLocation To = P->getIdentifier() ? P->getLocation() : P->getEndLoc();
    if (To.isMacroID() || SM.getFileID(From) != SM.getFileID(To))
      return false;
    unsigned End = SM.getFileOffset(To);
    StringRef Buf = SM.getBufferData(SM.getFileID(From));
    Lexer L(SM.getLocForStartOfFile(SM.getFileID(From)), LO, Buf.begin(),
            Buf.begin() + SM.getFileOffset(From), Buf.end());
    bool Dropped = false;
    Token T;
    while (!L.LexFromRawLexer(T) && SM.getFileOffset(T.getLocation()) < End) {
      if (!T.is(tok::raw_identifier))
        return false;
      StringRef Word = T.getRawIdentifier();
      if (Word != "const" && Word != "volatile" && Word != "restrict" &&
          Word != "__restrict" && Word != "__restrict__")
        return false;
      unsigned Len = T.getLength();
      const char *After = SM.getCharacterData(T.getLocation()) + Len;
      while (*After == ' ' || *After == '\t')
        ++After, ++Len;
      Cand.Edits.push_back(
          FixItHint::CreateRemoval(chars(T.getLocation(), Len)));
      Dropped = true;
    }
    return Dropped;
  }

  static bool simpleOperand(const Expr *E) {
    return isa<DeclRefExpr, MemberExpr, CallExpr, ArraySubscriptExpr, ParenExpr,
               CXXThisExpr>(E);
  }

  bool callEdits(Candidate &Cand) {
    Cand.LateEdits.clear();
    Cand.LateNeeds.clear();
    const FunctionDecl *Canon = Cand.FD->getCanonicalDecl();
    for (CallExpr *CE : Ctx.Visitor.Calls) {
      const FunctionDecl *Callee = CE->getDirectCallee();
      if (!Callee || Callee->getCanonicalDecl() != Canon)
        continue;
      if (isa<CXXOperatorCallExpr>(CE) || Cand.Index >= CE->getNumArgs())
        return false;
      const Expr *Arg = CE->getArg(Cand.Index);
      SourceLocation Begin = Arg->getBeginLoc(), End = Arg->getEndLoc();
      if (Begin.isMacroID() || End.isMacroID() ||
          Arg->isNullPointerConstant(Ctx.C, Expr::NPC_ValueDependentIsNotNull))
        return false;
      for (unsigned I = 0, N = CE->getNumArgs(); I != N; ++I)
        if (I != Cand.Index && CE->getArg(I)->HasSideEffects(Ctx.C))
          return false;
      const Expr *Inner = Arg->IgnoreParenImpCasts();
      if (const auto *ICE = dyn_cast<ImplicitCastExpr>(Arg->IgnoreParens()))
        if (ICE->getCastKind() == CK_ArrayToPointerDecay)
          return false;
      // A parameter passed straight through needs no edit while it becomes a
      // reference too.
      if (const auto *DRE = dyn_cast<DeclRefExpr>(Inner))
        if (const auto *P = dyn_cast<ParmVarDecl>(DRE->getDecl()))
          if (Ctx.live(P, Id)) {
            Cand.LateNeeds.push_back(P);
            continue;
          }
      if (const auto *UO = dyn_cast<UnaryOperator>(Inner);
          UO && UO->getOpcode() == UO_AddrOf) {
        Cand.LateEdits.push_back(
            FixItHint::CreateRemoval(chars(UO->getOperatorLoc(), 1)));
        continue;
      }
      if (simpleOperand(Inner)) {
        Cand.LateEdits.push_back(FixItHint::CreateInsertion(Begin, "*"));
        continue;
      }
      Cand.LateEdits.push_back(FixItHint::CreateInsertion(Begin, "*("));
      Cand.LateEdits.push_back(FixItHint::CreateInsertion(
          Lexer::getLocForEndOfToken(End, 0, Ctx.SM, Ctx.C.getLangOpts()),
          ")"));
    }
    return true;
  }

  llvm::StringMap<llvm::DenseMap<unsigned, cir::NullabilityFact>> Facts;
};

} // namespace

std::unique_ptr<Rewrite>
clang::cirmodernize::createParamToReference(Context &Ctx, unsigned Id) {
  return std::make_unique<ParamToReference>(Ctx, Id);
}
