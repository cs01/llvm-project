//===--- PointerToConst.cpp - Pointer parameter to pointer to const -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pointer parameter to pointer to const. CIR says whether anything writes
// through the parameter (CIRPointeeWrites); the AST says which callee
// arguments are read-only, since CIR keeps no const qualifiers. A parameter
// passed on to another non-const pointer parameter needs that one to become
// const too. The only edit is `const ` before the pointee in every
// declaration; calls need none, as T * converts to const T *.
//
//===----------------------------------------------------------------------===//

#include "CIRModernizeFramework.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/CIR/Dialect/Analysis/CIRPointeeWrites.h"

using namespace clang;
using namespace clang::cirmodernize;

namespace {

class PointerToConst : public Rewrite {
public:
  using Rewrite::Rewrite;

  StringRef name() const override { return "const"; }
  unsigned diagID(DiagnosticsEngine &Diags) const override {
    return Diags.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "pointer parameter %0 of %1 is never written through; it can "
        "point to const");
  }
  StringRef whyNeedBlocked() const override {
    return "const: forwarded to a parameter that stays non-const";
  }

  void analyze(mlir::ModuleOp Module) override {
    cir::collectPointeeWrites(
        Module,
        [&](cir::FuncOp Callee, unsigned Index) {
          return argUse(Callee, Index);
        },
        [&](const cir::PointeeWriteFact &F) {
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
    unsigned Offset = Context::thisOffset(FD);
    llvm::SmallVector<unsigned> Indices;
    if (It != Facts.end())
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
      StringRef Why = whyParamIneligible(P);
      if (Why == AlreadyConst)
        continue;
      Ctx.count("const: pointer parameters");
      if (Why.empty())
        Why = FunctionWhy;
      Candidate Cand{FD, P, Index, Id};
      if (Why.empty() && It != Facts.end())
        if (auto Fact = It->second.find(Index + Offset);
            Fact != It->second.end()) {
          if (Fact->second.written)
            Why = Ctx.save("const: " + Fact->second.why);
          for (auto &[Callee, CalleeIndex] : Fact->second.forwards) {
            const FunctionDecl *CFD = Ctx.declByMangledName(Callee);
            unsigned CalleeOffset = CFD ? Context::thisOffset(CFD) : 0;
            ParmVarDecl *Needed =
                CFD && CalleeIndex >= CalleeOffset
                    ? Context::keyParam(CFD, CalleeIndex - CalleeOffset)
                    : nullptr;
            if (!Needed && Why.empty())
              Why = "const: forwarded to an unknown function";
            if (Needed)
              Cand.Needs.push_back(Needed);
          }
        }
      if (Why.empty() && usedInFoldedBranch(P))
        Why = "const: used in a branch the build folds away";
      if (Why.empty() && !declEdits(Cand))
        Why = "const: pointer spelled through a typedef or macro";
      Ctx.add(std::move(Cand), Why);
    }
  }

  void addExternal(FunctionDecl *FD, unsigned Index) override {
    ParmVarDecl *P = FD->getParamDecl(Index);
    StringRef Why = whyParamIneligible(P);
    if (Why.empty())
      Why = Ctx.whyIneligible(FD);
    Candidate Cand{FD, P, Index, Id};
    Cand.External = true;
    if (Why.empty() && !declEdits(Cand))
      Why = "const: pointer spelled through a typedef or macro";
    Ctx.add(std::move(Cand), Why);
  }

protected:
  // CIRGen emits nothing for the dead branch of `if (0)`, so the facts never
  // see a use there, yet the branch is still type-checked: passing the
  // pointer on to a non-const parameter there stops compiling (C++) or warns
  // (C) once the pointee is const.
  bool usedInFoldedBranch(const ParmVarDecl *P) {
    for (DeclRefExpr *DRE : Ctx.Visitor.Refs) {
      if (DRE->getDecl() != P)
        continue;
      DynTypedNode Node = DynTypedNode::create(*DRE);
      for (DynTypedNodeList Parents = Ctx.C.getParents(Node); !Parents.empty();
           Parents = Ctx.C.getParents(Node)) {
        if (const auto *If = Parents[0].get<IfStmt>()) {
          bool Cond;
          const Stmt *Child = Node.get<Stmt>();
          if (!If->getCond()->isValueDependent() &&
              If->getCond()->EvaluateAsBooleanCondition(Cond, Ctx.C) &&
              Child == (Cond ? If->getElse() : If->getThen()))
            return true;
        }
        Node = Parents[0];
      }
    }
    return false;
  }

  static constexpr StringRef AlreadyConst = "const: already const";

  static StringRef whyParamIneligible(const ParmVarDecl *P) {
    const auto *PT = P->getType()->getAs<PointerType>();
    if (!PT)
      return "const: not a plain pointer";
    QualType Pointee = PT->getPointeeType();
    if (Pointee.isConstQualified())
      return AlreadyConst;
    if (Pointee->isFunctionType() || Pointee->isArrayType())
      return "const: pointer to function or array";
    return {};
  }

  // `const ` before the pointee of every declaration this translation unit
  // sees, or after the pointee's own '*' when it is a pointer.
  bool declEdits(Candidate &Cand) {
    for (const FunctionDecl *R : Cand.FD->redecls()) {
      std::optional<PointerTypeLoc> Star =
          Context::starOf(R->getParamDecl(Cand.Index));
      if (!Star || Star->getStarLoc().isMacroID())
        return false;
      // A pointee that is itself a pointer takes const after its own '*'
      // (char *const *); in front, const would qualify what that points to.
      // The inner pointer may be qualified itself (T *restrict *p).
      if (auto Inner = Star->getPointeeLoc()
                           .getUnqualifiedLoc()
                           .getAs<PointerTypeLoc>()) {
        SourceLocation InnerStar = Inner.getStarLoc();
        if (InnerStar.isInvalid() || InnerStar.isMacroID())
          return false;
        Cand.Edits.push_back(FixItHint::CreateInsertion(
            InnerStar.getLocWithOffset(1), "const "));
        continue;
      }
      SourceLocation Begin = Star->getPointeeLoc().getBeginLoc();
      if (Begin.isInvalid() || Begin.isMacroID())
        return false;
      Cand.Edits.push_back(FixItHint::CreateInsertion(Begin, "const "));
    }
    return true;
  }

  // Whether a callee only reads through its CIR argument Index. CIR keeps no
  // const qualifiers, so the answer comes from the callee's declaration.
  cir::PointerArgUse argUse(cir::FuncOp Callee, unsigned Index) {
    const FunctionDecl *FD = Ctx.declByMangledName(Callee.getSymName());
    if (!FD)
      return cir::PointerArgUse::Writes;
    unsigned Offset = Context::thisOffset(FD);
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

  llvm::StringMap<llvm::DenseMap<unsigned, cir::PointeeWriteFact>> Facts;
};

// `const` on a method that never writes through `this`: the same fact, about
// CIR argument 0. Whatever `this` is passed to must only read, so a call to
// a non-const method, a store of `this` or of an address into the object, or
// returning one, keeps the method as it is.
class ConstMethod : public PointerToConst {
public:
  using PointerToConst::PointerToConst;

  StringRef name() const override { return "const-method"; }
  unsigned diagID(DiagnosticsEngine &Diags) const override {
    return Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                 "method %0 never writes through 'this'; it "
                                 "can be const");
  }
  StringRef whyNeedBlocked() const override { return "const-method: needed"; }

  void addDefined(FunctionDecl *FD) override {
    auto *MD = dyn_cast<CXXMethodDecl>(FD);
    if (!MD || !MD->isInstance() || MD->isConst() ||
        isa<CXXConstructorDecl, CXXDestructorDecl>(MD) ||
        MD->isDependentContext() || MD->isTemplated() ||
        MD->getParent()->isLambda())
      return;
    auto It = Facts.find(Ctx.mangle(FD));
    const llvm::DenseSet<unsigned> *Planned = Ctx.planned(FD, name());
    bool Proven = false;
    StringRef Why;
    if (It != Facts.end())
      if (auto Fact = It->second.find(0); Fact != It->second.end()) {
        Proven = true;
        if (Fact->second.written)
          Why = Ctx.save("const-method: " + Fact->second.why);
        else if (!Fact->second.forwards.empty())
          Why = "const-method: this passed to a non-const pointer parameter";
      }
    if (!Proven && !(Planned && Planned->contains(0)))
      return;
    Ctx.count("const-method: methods");
    if (Why.empty())
      Why = Ctx.whyIneligible(FD);
    add(MD, Why, /*External=*/false);
  }

  void addExternal(FunctionDecl *FD, unsigned) override {
    auto *MD = dyn_cast<CXXMethodDecl>(FD);
    if (MD && MD->isInstance() && !MD->isConst())
      add(MD, Ctx.whyIneligible(FD), /*External=*/true);
  }

private:
  void add(CXXMethodDecl *MD, StringRef Why, bool External) {
    Candidate Cand{MD, nullptr, 0, Id};
    Cand.External = External;
    if (Why.empty() && !methodEdits(Cand))
      Why = "const-method: declarator in a macro";
    Ctx.add(std::move(Cand), Why);
  }

  // ` const` after the parameter list of every declaration, before any
  // ref-qualifier or exception specification.
  bool methodEdits(Candidate &Cand) {
    for (const FunctionDecl *R : Cand.FD->redecls()) {
      FunctionTypeLoc FTL = R->getFunctionTypeLoc();
      SourceLocation RParen = FTL ? FTL.getRParenLoc() : SourceLocation();
      if (RParen.isInvalid() || RParen.isMacroID())
        return false;
      Cand.Edits.push_back(
          FixItHint::CreateInsertion(RParen.getLocWithOffset(1), " const"));
    }
    return true;
  }
};

} // namespace

std::unique_ptr<Rewrite>
clang::cirmodernize::createPointerToConst(Context &Ctx, unsigned Id) {
  return std::make_unique<PointerToConst>(Ctx, Id);
}

std::unique_ptr<Rewrite> clang::cirmodernize::createConstMethod(Context &Ctx,
                                                                unsigned Id) {
  return std::make_unique<ConstMethod>(Ctx, Id);
}
