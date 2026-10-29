//===--- Span.cpp - Pointer and length parameters to a view ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Turns adjacent parameters `T *p, size_t n` into one view parameter `p`
// (std::span<T>, or llvm::ArrayRef<T> / MutableArrayRef<T> with
// -fcir-modernize-view=llvm) when the body only indexes `p` and only reads
// `n`. Uses of `n` become `p.size()`; a call f(a, b) becomes
// f(View<T>(a, b)), keeping the argument text and its comma.
//
// The view's operator[] may check its index, so the compiled code changes:
// this is the opt-in refinement group, proven by cir-modernize-ircmp
// --refine. That proof accepts the rewrite only when every added check folds
// away against a guard the code already has, so the length is the real
// bound wherever the code relies on it.
//
// The view type must already be declared in the translation unit; adding an
// #include is not one of the edits.
//
//===----------------------------------------------------------------------===//

#include "CIRModernizeFramework.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/Lex/Lexer.h"

using namespace clang;
using namespace clang::cirmodernize;

namespace {

class Span : public Rewrite {
public:
  using Rewrite::Rewrite;

  StringRef name() const override { return "span"; }
  unsigned diagID(DiagnosticsEngine &Diags) const override {
    return Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                 "pointer parameter %0 of %1 and the length "
                                 "after it can be one view");
  }
  StringRef whyNeedBlocked() const override { return "span: needed"; }
  bool refines() const override { return true; }

  // Nothing comes from CIR here: the proof decides.
  void analyze(mlir::ModuleOp) override {}

  void addDefined(FunctionDecl *FD) override {
    if (!FD->hasBody())
      return;
    for (unsigned Index = 0; Index + 1 < FD->getNumParams(); ++Index) {
      ParmVarDecl *P = FD->getParamDecl(Index),
                  *N = FD->getParamDecl(Index + 1);
      if (!pair(P, N))
        continue;
      Ctx.count("span: pointer and length pairs");
      StringRef Why = Ctx.whyIneligible(FD);
      if (Why.empty())
        Why = Ctx.whyVisibleToC(FD);
      if (Why.empty())
        Why = whyBody(FD, P, N);
      add(FD, Index, Why, /*External=*/false);
    }
  }

  void addExternal(FunctionDecl *FD, unsigned Index) override {
    if (Index + 1 >= FD->getNumParams() ||
        !pair(FD->getParamDecl(Index), FD->getParamDecl(Index + 1)))
      return;
    StringRef Why = Ctx.whyIneligible(FD);
    if (Why.empty())
      Why = Ctx.whyVisibleToC(FD);
    add(FD, Index, Why, /*External=*/true);
  }

private:
  bool pair(const ParmVarDecl *P, const ParmVarDecl *N) const {
    const auto *PT = P->getType()->getAs<PointerType>();
    if (!PT || !P->getIdentifier())
      return false;
    QualType T = PT->getPointeeType();
    return T->isObjectType() && !T->isVoidType() && !T->isArrayType() &&
           Ctx.C.hasSameType(N->getType().getUnqualifiedType(),
                             Ctx.C.getSizeType());
  }

  // `p` only as the base of p[e], `n` only read.
  StringRef whyBody(FunctionDecl *FD, ParmVarDecl *P, ParmVarDecl *N) {
    for (DeclRefExpr *DRE : Ctx.Visitor.Refs) {
      if (DRE->getDecl() != P && DRE->getDecl() != N)
        continue;
      const Expr *Use = DRE;
      const Stmt *Parent = parent(Use);
      if (DRE->getDecl() == N) {
        const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(Parent);
        if (!Cast || Cast->getCastKind() != CK_LValueToRValue)
          return "span: the length is written or its address taken";
        continue;
      }
      const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(Parent);
      const auto *Sub = Cast && Cast->getCastKind() == CK_LValueToRValue
                            ? dyn_cast_or_null<ArraySubscriptExpr>(parent(Cast))
                            : nullptr;
      if (!Sub || Sub->getBase()->IgnoreImpCasts() != DRE)
        return "span: the pointer is used other than as p[e]";
    }
    (void)FD;
    return {};
  }

  const Stmt *parent(const Stmt *S) {
    DynTypedNodeList Parents = Ctx.C.getParents(*S);
    return Parents.empty() ? nullptr : Parents[0].get<Stmt>();
  }

  // The view type for pointee T, or empty when it is not declared here.
  std::string view(QualType T) {
    bool Llvm = Ctx.Options.View == "llvm";
    StringRef Ns = Llvm ? "llvm" : "std";
    StringRef Name =
        Llvm ? (T.isConstQualified() ? "ArrayRef" : "MutableArrayRef") : "span";
    if (!declared(Ns, Name))
      return {};
    PrintingPolicy PP(Ctx.C.getLangOpts());
    PP.SuppressTagKeyword = true;
    QualType Elem = Llvm ? T.getUnqualifiedType() : T;
    return (Ns + "::" + Name + "<" + Elem.getAsString(PP) + ">").str();
  }

  bool declared(StringRef Ns, StringRef Name) {
    TranslationUnitDecl *TU = Ctx.C.getTranslationUnitDecl();
    for (NamedDecl *D : TU->lookup(&Ctx.C.Idents.get(Ns)))
      if (auto *NS = dyn_cast<NamespaceDecl>(D))
        for (NamedDecl *C :
             NS->getPrimaryContext()->lookup(&Ctx.C.Idents.get(Name)))
          if (isa<ClassTemplateDecl>(C))
            return true;
    // std::span lives in an inline namespace (std::__1) in libc++.
    for (NamedDecl *D : TU->lookup(&Ctx.C.Idents.get(Ns)))
      if (auto *NS = dyn_cast<NamespaceDecl>(D))
        for (Decl *Inner : NS->decls())
          if (auto *INS = dyn_cast<NamespaceDecl>(Inner);
              INS && INS->isInline())
            for (NamedDecl *C : INS->lookup(&Ctx.C.Idents.get(Name)))
              if (isa<ClassTemplateDecl>(C))
                return true;
    return false;
  }

  void add(FunctionDecl *FD, unsigned Index, StringRef Why, bool External) {
    Candidate Cand{FD, FD->getParamDecl(Index), Index, Id};
    Cand.External = External;
    if (Why.empty()) {
      std::string View =
          view(FD->getParamDecl(Index)->getType()->getPointeeType());
      if (View.empty())
        Why = "span: the view type is not declared in this file";
      else if (!edits(Cand, View, External))
        Why = "span: a declaration or call has an unhandled shape";
    }
    Ctx.add(std::move(Cand), Why);
  }

  // The apply path and the project export take character ranges; a token
  // range over one token would look empty to them.
  CharSourceRange charRange(SourceRange R) {
    return CharSourceRange::getCharRange(
        R.getBegin(),
        Lexer::getLocForEndOfToken(R.getEnd(), 0, Ctx.SM, Ctx.C.getLangOpts()));
  }

  bool edits(Candidate &Cand, StringRef View, bool External) {
    const LangOptions &LO = Ctx.C.getLangOpts();
    for (const FunctionDecl *R : Cand.FD->redecls()) {
      const ParmVarDecl *P = R->getParamDecl(Cand.Index);
      const ParmVarDecl *N = R->getParamDecl(Cand.Index + 1);
      SourceRange Range(P->getBeginLoc(), N->getEndLoc());
      if (Range.getBegin().isMacroID() || Range.getEnd().isMacroID() ||
          N->hasDefaultArg())
        return false;
      std::string Name =
          P->getIdentifier() ? " " + P->getName().str() : std::string();
      Cand.Edits.push_back(
          FixItHint::CreateReplacement(charRange(Range), (View + Name).str()));
    }
    // In the body the length is the view's size.
    if (!External)
      if (const FunctionDecl *Def = Cand.FD->getDefinition()) {
        const ParmVarDecl *P = Def->getParamDecl(Cand.Index);
        const ParmVarDecl *N = Def->getParamDecl(Cand.Index + 1);
        for (DeclRefExpr *DRE : Ctx.Visitor.Refs)
          if (DRE->getDecl() == N) {
            if (DRE->getLocation().isMacroID())
              return false;
            Cand.Edits.push_back(
                FixItHint::CreateReplacement(charRange(DRE->getSourceRange()),
                                             (P->getName() + ".size()").str()));
          }
      }
    const FunctionDecl *Canon = Cand.FD->getCanonicalDecl();
    for (CallExpr *CE : Ctx.Visitor.Calls) {
      const FunctionDecl *Callee = CE->getDirectCallee();
      if (!Callee || Callee->getCanonicalDecl() != Canon)
        continue;
      if (isa<CXXOperatorCallExpr>(CE) || Cand.Index + 1 >= CE->getNumArgs())
        return false;
      const Expr *A = CE->getArg(Cand.Index), *B = CE->getArg(Cand.Index + 1);
      SourceLocation Begin = A->getBeginLoc();
      SourceLocation End =
          Lexer::getLocForEndOfToken(B->getEndLoc(), 0, Ctx.SM, LO);
      if (Begin.isMacroID() || End.isInvalid() || End.isMacroID() ||
          A->isNullPointerConstant(Ctx.C, Expr::NPC_ValueDependentIsNotNull))
        return false;
      Cand.Edits.push_back(
          FixItHint::CreateInsertion(Begin, (View + "(").str()));
      Cand.Edits.push_back(FixItHint::CreateInsertion(End, ")"));
    }
    return true;
  }
};

} // namespace

std::unique_ptr<Rewrite> clang::cirmodernize::createSpan(Context &Ctx,
                                                         unsigned Id) {
  return std::make_unique<Span>(Ctx, Id);
}
