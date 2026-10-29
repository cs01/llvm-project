//===--- LockGuard.cpp - lock()/unlock() pairs to std::lock_guard ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// In a block whose last statement is `m.unlock();`, an earlier `m.lock();` on
// the same object becomes `std::lock_guard<M> guard_m(m);` and the unlock
// goes. The guard then also unlocks on every other way out of the block:
// an early return, a break, an exception. Where the original left the mutex
// locked on such a path, the compiled code differs, cir-modernize-ircmp
// --refine finds it, and the rewrite is refused; so an accepted rewrite only
// moves the unlock into a destructor, and future early returns get it too.
//
// Opt-in, in the refinement group: the code is equal only after inlining
// the guard.
//
//===----------------------------------------------------------------------===//

#include "CIRModernizeFramework.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Lex/Lexer.h"

using namespace clang;
using namespace clang::cirmodernize;

namespace {

class LockGuard : public Rewrite {
public:
  using Rewrite::Rewrite;

  StringRef name() const override { return "lock-guard"; }
  unsigned diagID(DiagnosticsEngine &Diags) const override {
    return Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                 "the lock() and unlock() in %1 can be a "
                                 "std::lock_guard");
  }
  StringRef whyNeedBlocked() const override { return "lock-guard: needed"; }
  bool refines() const override { return true; }

  void analyze(mlir::ModuleOp) override {}

  void addDefined(FunctionDecl *FD) override {
    if (!FD->hasBody() || FD->isTemplated() || FD->isDependentContext())
      return;
    unsigned Ordinal = 0;
    walk(FD, FD->getBody(), Ordinal);
  }

  // A plan names a function; its lock sites are all in its body, which only
  // the defining translation unit has.
  void addExternal(FunctionDecl *, unsigned) override {}

protected:
  virtual void block(FunctionDecl *FD, CompoundStmt *CS, unsigned &Ordinal) {
    lockPair(FD, CS, Ordinal);
  }

  void walk(FunctionDecl *FD, Stmt *S, unsigned &Ordinal) {
    if (!S)
      return;
    if (auto *CS = dyn_cast<CompoundStmt>(S))
      block(FD, CS, Ordinal);
    for (Stmt *Child : S->children())
      if (Child && !isa<LambdaExpr>(Child))
        walk(FD, Child, Ordinal);
  }

  // `obj.lock()` or `obj.unlock()` on a named object: its declaration.
  static const ValueDecl *lockCall(const Stmt *S, StringRef Method) {
    const auto *MC = dyn_cast<CXXMemberCallExpr>(S);
    const CXXMethodDecl *MD = MC ? MC->getMethodDecl() : nullptr;
    if (!MD || !MD->getIdentifier() || MD->getName() != Method ||
        MC->getNumArgs())
      return nullptr;
    const auto *DRE = dyn_cast<DeclRefExpr>(
        MC->getImplicitObjectArgument()->IgnoreParenImpCasts());
    return DRE ? DRE->getDecl() : nullptr;
  }

  void lockPair(FunctionDecl *FD, CompoundStmt *CS, unsigned &Ordinal) {
    if (CS->body_empty())
      return;
    const ValueDecl *M = lockCall(CS->body_back(), "unlock");
    if (!M)
      return;
    // The last lock() of M before the unlock, with no other lock or unlock
    // of M between.
    const Stmt *Lock = nullptr;
    for (Stmt *S : CS->body()) {
      if (S == CS->body_back())
        break;
      if (lockCall(S, "lock") == M)
        Lock = S;
      else if (lockCall(S, "unlock") == M)
        Lock = nullptr;
    }
    if (!Lock)
      return;
    Ctx.count("lock-guard: lock and unlock pairs");
    Candidate Cand{FD, nullptr, Ordinal++, Id};
    StringRef Why = Ctx.whyVisibleToC(FD);
    QualType T = M->getType().getNonReferenceType().getUnqualifiedType();
    std::string Guard = ("guard_" + M->getName()).str();
    if (Why.empty() && !declared("std", "lock_guard"))
      Why = "lock-guard: std::lock_guard is not declared in this file";
    if (Why.empty() && !T->getAsCXXRecordDecl())
      Why = "lock-guard: the mutex is not a class object";
    if (Why.empty() && nameTaken(FD, Guard))
      Why = "lock-guard: the guard's name is taken";
    if (Why.empty()) {
      PrintingPolicy PP(Ctx.C.getLangOpts());
      PP.SuppressTagKeyword = true;
      std::string Decl = "std::lock_guard<" + T.getAsString(PP) + "> " + Guard +
                         "(" + M->getName().str() + ");";
      SourceLocation LockEnd = semi(Lock), UnlockEnd = semi(CS->body_back());
      if (LockEnd.isInvalid() || UnlockEnd.isInvalid() ||
          Lock->getBeginLoc().isMacroID() ||
          CS->body_back()->getBeginLoc().isMacroID())
        Why = "lock-guard: the calls are in a macro";
      else {
        Cand.Edits.push_back(FixItHint::CreateReplacement(
            CharSourceRange::getCharRange(Lock->getBeginLoc(), LockEnd), Decl));
        Cand.Edits.push_back(FixItHint::CreateRemoval(
            wholeLine(CS->body_back()->getBeginLoc(), UnlockEnd)));
      }
    }
    Ctx.add(std::move(Cand), Why);
  }

  // Just past the ';' that ends the expression statement S.
  SourceLocation semi(const Stmt *S) {
    SourceLocation End = Lexer::getLocForEndOfToken(S->getEndLoc(), 0, Ctx.SM,
                                                    Ctx.C.getLangOpts());
    if (End.isInvalid())
      return {};
    std::optional<Token> Tok =
        Lexer::findNextToken(S->getEndLoc(), Ctx.SM, Ctx.C.getLangOpts());
    if (!Tok || !Tok->is(tok::semi))
      return {};
    return Tok->getEndLoc();
  }

  // [Begin, End), widened to its whole line when nothing else is on it.
  CharSourceRange wholeLine(SourceLocation Begin, SourceLocation End) {
    auto [FID, B] = Ctx.SM.getDecomposedLoc(Begin);
    unsigned E = Ctx.SM.getFileOffset(End);
    StringRef Buf = Ctx.SM.getBufferData(FID);
    unsigned LineStart = B;
    while (LineStart &&
           (Buf[LineStart - 1] == ' ' || Buf[LineStart - 1] == '\t'))
      --LineStart;
    unsigned LineEnd = E;
    while (LineEnd < Buf.size() &&
           (Buf[LineEnd] == ' ' || Buf[LineEnd] == '\t'))
      ++LineEnd;
    if ((LineStart == 0 || Buf[LineStart - 1] == '\n') &&
        LineEnd < Buf.size() && Buf[LineEnd] == '\n')
      return CharSourceRange::getCharRange(
          Begin.getLocWithOffset(-int(B - LineStart)),
          End.getLocWithOffset(LineEnd + 1 - E));
    return CharSourceRange::getCharRange(Begin, End);
  }

  bool nameTaken(const FunctionDecl *FD, StringRef Name) {
    for (DeclRefExpr *DRE : Ctx.Visitor.Refs)
      if (DRE->getDecl()->getIdentifier() && DRE->getDecl()->getName() == Name)
        return true;
    for (const ParmVarDecl *P : FD->parameters())
      if (P->getIdentifier() && P->getName() == Name)
        return true;
    return false;
  }

  bool declared(StringRef Ns, StringRef Name) {
    TranslationUnitDecl *TU = Ctx.C.getTranslationUnitDecl();
    auto Find = [&](DeclContext *DC) {
      for (NamedDecl *C : DC->lookup(&Ctx.C.Idents.get(Name)))
        if (isa<ClassTemplateDecl>(C))
          return true;
      return false;
    };
    for (NamedDecl *D : TU->lookup(&Ctx.C.Idents.get(Ns)))
      if (auto *NS = dyn_cast<NamespaceDecl>(D)) {
        if (Find(NS->getPrimaryContext()))
          return true;
        // libc++ keeps std in an inline namespace (std::__1).
        for (Decl *Inner : NS->decls())
          if (auto *INS = dyn_cast<NamespaceDecl>(Inner);
              INS && INS->isInline() && Find(INS))
            return true;
      }
    return false;
  }
};

// A local `T *p = new T(...)` whose block ends with `delete p;` (or with it
// and then a return that does not name p) becomes
// `std::unique_ptr<T> p(new T(...));` and the delete goes. p may only be
// dereferenced (p->m, *p), which a unique_ptr spells the same way. Where
// something between can throw, the original leaks and the unique_ptr does
// not; the proof sees the added cleanup and refuses.
class UniquePtr : public LockGuard {
public:
  using LockGuard::LockGuard;

  StringRef name() const override { return "unique-ptr"; }
  unsigned diagID(DiagnosticsEngine &Diags) const override {
    return Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                 "the new and delete in %1 can be a "
                                 "std::unique_ptr");
  }
  StringRef whyNeedBlocked() const override { return "unique-ptr: needed"; }

private:
  void block(FunctionDecl *FD, CompoundStmt *CS, unsigned &Ordinal) override {
    auto Body = CS->body();
    SmallVector<Stmt *> Stmts(Body.begin(), Body.end());
    if (Stmts.empty())
      return;
    // The delete closes the block, or is followed only by a return.
    unsigned Last = Stmts.size() - 1;
    const auto *Ret = dyn_cast<ReturnStmt>(Stmts[Last]);
    unsigned DelAt = Ret && Last ? Last - 1 : Last;
    const auto *Del = dyn_cast<CXXDeleteExpr>(Stmts[DelAt]);
    if (!Del || Del->isArrayForm())
      return;
    const auto *DRE =
        dyn_cast<DeclRefExpr>(Del->getArgument()->IgnoreParenImpCasts());
    const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
    if (!VD || !VD->isLocalVarDecl())
      return;
    const DeclStmt *DS = nullptr;
    for (unsigned I = 0; I < DelAt; ++I)
      if (const auto *D = dyn_cast<DeclStmt>(Stmts[I]);
          D && D->isSingleDecl() && D->getSingleDecl() == VD)
        DS = D;
    const auto *New =
        DS ? dyn_cast_or_null<CXXNewExpr>(VD->getInit()->IgnoreImpCasts())
           : nullptr;
    if (!New || New->isArray() || New->getNumPlacementArgs())
      return;
    Ctx.count("unique-ptr: new and delete pairs");
    Candidate Cand{FD, nullptr, Ordinal++, Id};
    StringRef Why = Ctx.whyVisibleToC(FD);
    if (Why.empty() && !declared("std", "unique_ptr"))
      Why = "unique-ptr: std::unique_ptr is not declared in this file";
    if (Why.empty() && !onlyDereferenced(VD, DRE, Ret))
      Why = "unique-ptr: the pointer is used other than as p-> or *p";
    if (Why.empty()) {
      PrintingPolicy PP(Ctx.C.getLangOpts());
      PP.SuppressTagKeyword = true;
      std::string Type = "std::unique_ptr<" +
                         New->getAllocatedType().getAsString(PP) + "> " +
                         VD->getName().str() + "(";
      SourceLocation InitEnd = Lexer::getLocForEndOfToken(
          VD->getInit()->getEndLoc(), 0, Ctx.SM, Ctx.C.getLangOpts());
      SourceLocation DelEnd = semi(Del);
      if (VD->getBeginLoc().isMacroID() || InitEnd.isInvalid() ||
          InitEnd.isMacroID() || DelEnd.isInvalid() ||
          Del->getBeginLoc().isMacroID())
        Why = "unique-ptr: the declaration or delete is in a macro";
      else {
        Cand.Edits.push_back(FixItHint::CreateReplacement(
            CharSourceRange::getCharRange(VD->getBeginLoc(),
                                          VD->getInit()->getBeginLoc()),
            Type));
        Cand.Edits.push_back(FixItHint::CreateInsertion(InitEnd, ")"));
        Cand.Edits.push_back(
            FixItHint::CreateRemoval(wholeLine(Del->getBeginLoc(), DelEnd)));
      }
    }
    Ctx.add(std::move(Cand), Why);
  }

  // Every use but the delete is p->m or *p, and the return does not name p.
  bool onlyDereferenced(const VarDecl *VD, const DeclRefExpr *Deleted,
                        const ReturnStmt *Ret) {
    for (DeclRefExpr *DRE : Ctx.Visitor.Refs) {
      if (DRE->getDecl() != VD || DRE == Deleted)
        continue;
      if (Ret && Ret->getSourceRange().fullyContains(DRE->getSourceRange()))
        return false;
      DynTypedNodeList Parents = Ctx.C.getParents(*DRE);
      const auto *Cast =
          Parents.empty() ? nullptr : Parents[0].get<ImplicitCastExpr>();
      if (!Cast || Cast->getCastKind() != CK_LValueToRValue)
        return false;
      DynTypedNodeList Up = Ctx.C.getParents(*Cast);
      if (Up.empty())
        return false;
      if (const auto *ME = Up[0].get<MemberExpr>(); ME && ME->isArrow())
        continue;
      if (const auto *UO = Up[0].get<UnaryOperator>();
          UO && UO->getOpcode() == UO_Deref)
        continue;
      return false;
    }
    return true;
  }
};

} // namespace

std::unique_ptr<Rewrite> clang::cirmodernize::createLockGuard(Context &Ctx,
                                                              unsigned Id) {
  return std::make_unique<LockGuard>(Ctx, Id);
}

std::unique_ptr<Rewrite> clang::cirmodernize::createUniquePtr(Context &Ctx,
                                                              unsigned Id) {
  return std::make_unique<UniquePtr>(Ctx, Id);
}
