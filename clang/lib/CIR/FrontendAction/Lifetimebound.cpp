//===--- Lifetimebound.cpp - [[clang::lifetimebound]] on parameters -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Marks a pointer or reference parameter [[clang::lifetimebound]] when CIR
// shows the returned pointer or reference can be derived from it, and, as
// the lifetimebound-this rewrite, a method whose result can be derived from
// `this`: an accessor returning a reference to a member. Clang then
// diagnoses callers that keep the result past the argument's lifetime
// (-Wdangling, -Wreturn-stack-address, -Wlifetime-safety).
//
// The attribute does not change codegen, so the IR identity proof holds. An
// annotation that is right can still make a caller warn: that warning is a
// dangling use the annotation exposed, which the proof reports as a new
// warning and the project run lists.
//
//===----------------------------------------------------------------------===//

#include "CIRModernizeFramework.h"

#include "clang/AST/Attr.h"
#include "clang/CIR/Dialect/Analysis/CIRReturnOrigins.h"
#include "clang/Lex/Lexer.h"

using namespace clang;
using namespace clang::cirmodernize;

namespace {

class Lifetimebound : public Rewrite {
public:
  Lifetimebound(Context &Ctx, unsigned Id, bool This)
      : Rewrite(Ctx, Id), This(This) {}

  StringRef name() const override {
    return This ? "lifetimebound-this" : "lifetimebound";
  }
  unsigned diagID(DiagnosticsEngine &Diags) const override {
    if (This)
      return Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                   "the result of %0 can refer to the object "
                                   "%1 is called on; it can be "
                                   "[[clang::lifetimebound]]");
    return Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                 "the result of %1 can refer to what "
                                 "parameter %0 refers to; it can be "
                                 "[[clang::lifetimebound]]");
  }
  StringRef whyNeedBlocked() const override { return "lifetimebound: needed"; }
  bool changesType() const override { return false; }

  void analyze(mlir::ModuleOp Module) override {
    cir::collectReturnOrigins(
        Module,
        [&](cir::FuncOp Ctor, unsigned Index) { return carries(Ctor, Index); },
        [&](const cir::ReturnOriginFact &F) {
          Facts[F.function].insert(F.args.begin(), F.args.end());
        });
  }

  void addDefined(FunctionDecl *FD) override {
    QualType Ret = FD->getReturnType();
    if (!Ret->isPointerType() && !Ret->isReferenceType() && !isView(Ret))
      return;
    auto It = Facts.find(Ctx.mangle(FD));
    const llvm::DenseSet<unsigned> *Planned = Ctx.planned(FD, name());
    if (It == Facts.end() && !Planned)
      return;
    StringRef FunctionWhy = whyNot(FD);
    unsigned Offset = Context::thisOffset(FD);
    if (This) {
      bool Derived = Offset && It != Facts.end() && It->second.contains(0);
      if (!Derived && !(Planned && Planned->contains(0)))
        return;
      Ctx.count("lifetimebound-this: proven methods");
      addThis(FD, FunctionWhy, /*External=*/false);
      return;
    }
    for (unsigned Index = 0; Index < FD->getNumParams(); ++Index) {
      ParmVarDecl *P = FD->getParamDecl(Index);
      if (!annotatable(P))
        continue;
      bool Derived = It != Facts.end() && It->second.contains(Index + Offset);
      if (!Derived && !(Planned && Planned->contains(Index)))
        continue;
      Ctx.count("lifetimebound: proven parameters");
      Candidate Cand{FD, P, Index, Id};
      StringRef Why = FunctionWhy;
      if (Why.empty() && !declEdits(Cand))
        Why = "lifetimebound: parameter name is in a macro";
      Ctx.add(std::move(Cand), Why);
    }
  }

  void addExternal(FunctionDecl *FD, unsigned Index) override {
    if (This) {
      if (Context::thisOffset(FD))
        addThis(FD, whyNot(FD), /*External=*/true);
      return;
    }
    Candidate Cand{FD, FD->getParamDecl(Index), Index, Id};
    Cand.External = true;
    StringRef Why = whyNot(FD);
    if (Why.empty() && !declEdits(Cand))
      Why = "lifetimebound: parameter name is in a macro";
    Ctx.add(std::move(Cand), Why);
  }

private:
  static bool hasGsl(const CXXRecordDecl *RD, bool Owner) {
    if (!RD)
      return false;
    auto Has = [&](const Decl *D) {
      return Owner ? D->hasAttr<OwnerAttr>() : D->hasAttr<PointerAttr>();
    };
    if (Has(RD))
      return true;
    const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    return Spec && Has(Spec->getSpecializedTemplate()->getTemplatedDecl());
  }

  // A [[gsl::Pointer]] class such as llvm::StringRef, returned by value.
  static bool isView(QualType T) {
    return hasGsl(T->getAsCXXRecordDecl(), /*Owner=*/false);
  }

  // Whether constructing through this argument makes the object refer to
  // what the argument refers to: the parameter says so, or a view is made
  // from a reference to an owner (a StringRef from a std::string). A view's
  // copy constructor copies a pointer kept elsewhere, so it does not.
  bool carries(cir::FuncOp Callee, unsigned Index) {
    const auto *Ctor = dyn_cast_or_null<CXXConstructorDecl>(
        Ctx.declByMangledName(Callee.getSymName()));
    if (!Ctor || Index == 0 || Index - 1 >= Ctor->getNumParams())
      return false;
    const ParmVarDecl *P = Ctor->getParamDecl(Index - 1);
    if (P->hasAttr<LifetimeBoundAttr>())
      return true;
    QualType T = P->getType();
    return hasGsl(Ctor->getParent(), /*Owner=*/false) && T->isReferenceType() &&
           hasGsl(T->getPointeeType()->getAsCXXRecordDecl(), /*Owner=*/true);
  }

  void addThis(FunctionDecl *FD, StringRef Why, bool External) {
    if (FD->hasAttr<LifetimeBoundAttr>() ||
        cast<CXXMethodDecl>(FD)->getMethodQualifiers().hasVolatile())
      return;
    Candidate Cand{FD, nullptr, 0, Id};
    Cand.External = External;
    if (Why.empty() && !thisEdits(Cand))
      Why = "lifetimebound-this: declarator has a trailing return type or "
            "a macro";
    Ctx.add(std::move(Cand), Why);
  }

  // After the qualifiers and exception specification of every declaration:
  // the attribute then appertains to the implicit object parameter.
  bool thisEdits(Candidate &Cand) {
    for (const FunctionDecl *R : Cand.FD->redecls()) {
      FunctionTypeLoc FTL = R->getFunctionTypeLoc();
      if (!FTL)
        return false;
      if (const auto *FPT = R->getType()->getAs<FunctionProtoType>();
          FPT && FPT->hasTrailingReturn())
        return false;
      SourceLocation End = FTL.getLocalRangeEnd();
      if (End.isInvalid() || End.isMacroID())
        return false;
      SourceLocation After =
          Lexer::getLocForEndOfToken(End, 0, Ctx.SM, Ctx.C.getLangOpts());
      if (After.isInvalid())
        return false;
      Cand.Edits.push_back(
          FixItHint::CreateInsertion(After, " [[clang::lifetimebound]]"));
    }
    return true;
  }

  static bool annotatable(const ParmVarDecl *P) {
    QualType T = P->getType();
    return (T->isReferenceType() ||
            (T->isPointerType() && !T->isFunctionPointerType())) &&
           !P->hasAttr<LifetimeBoundAttr>();
  }

  // Overloads and mangled names do not matter here: the attribute changes
  // no signature. What does: facts for one instantiation are not facts for
  // the template, an override can return something else, and a
  // constructor's result is the object, not a returned value.
  StringRef whyNot(FunctionDecl *FD) {
    if (isa<CXXConstructorDecl, CXXDestructorDecl>(FD))
      return "special function";
    if (FD->isTemplated() || FD->isTemplateInstantiation() ||
        FD->getTemplatedKind() != FunctionDecl::TK_NonTemplate)
      return "template";
    if (auto *MD = dyn_cast<CXXMethodDecl>(FD))
      if (MD->isVirtual() || MD->size_overridden_methods())
        return "virtual or local class method";
    if (FD->isExternallyVisible() && !Ctx.Options.AllowPublic &&
        !Ctx.projectMode())
      return "externally visible";
    for (const FunctionDecl *R : FD->redecls())
      if (!Ctx.editable(R->getLocation()))
        return Ctx.projectMode() ? "declared in a system header or a macro"
                                 : "declared outside the main file";
    if (Ctx.projectMode() && Ctx.usr(FD).empty())
      return "no USR";
    if (!Ctx.C.getLangOpts().CPlusPlus11)
      return "lifetimebound: needs C++11 attribute syntax";
    return {};
  }

  // After the parameter's name in every declaration this TU sees; an
  // unnamed parameter gets it after its type.
  bool declEdits(Candidate &Cand) {
    for (const FunctionDecl *R : Cand.FD->redecls()) {
      const ParmVarDecl *P = R->getParamDecl(Cand.Index);
      SourceLocation Last =
          P->getDeclName() ? P->getLocation()
                           : P->getTypeSourceInfo()->getTypeLoc().getEndLoc();
      if (Last.isInvalid() || Last.isMacroID())
        return false;
      SourceLocation After =
          Lexer::getLocForEndOfToken(Last, 0, Ctx.SM, Ctx.C.getLangOpts());
      if (After.isInvalid())
        return false;
      Cand.Edits.push_back(
          FixItHint::CreateInsertion(After, " [[clang::lifetimebound]]"));
    }
    return true;
  }

  const bool This;
  llvm::StringMap<llvm::DenseSet<unsigned>> Facts;
};

} // namespace

std::unique_ptr<Rewrite> clang::cirmodernize::createLifetimebound(Context &Ctx,
                                                                  unsigned Id) {
  return std::make_unique<Lifetimebound>(Ctx, Id, /*This=*/false);
}

std::unique_ptr<Rewrite>
clang::cirmodernize::createLifetimeboundThis(Context &Ctx, unsigned Id) {
  return std::make_unique<Lifetimebound>(Ctx, Id, /*This=*/true);
}
