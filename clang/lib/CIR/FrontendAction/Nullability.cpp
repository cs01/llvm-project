//===--- Nullability.cpp - Nullability qualifiers on pointer parameters ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Spells what CIR proved about a pointer parameter in its type, for the
// parameters that stay pointers:
//
//  * _Nonnull when the parameter is dereferenced on every path (the fact the
//    reference rewrite uses), so a null argument is already undefined
//    behavior. It suits what that rewrite cannot change: C files and C
//    linkage, pointer arithmetic on the parameter, a declarator it cannot
//    edit.
//  * _Nullable when the function compares the parameter with null and no
//    dereference is unchecked, so null is a value it handles.
//
// Neither changes codegen, so the IR identity proof holds; callers need no
// edit. The qualifiers are opt-in (-fcir-modernize-only=nullability) because
// they are a clang extension, and because -Wnullability-completeness then
// expects every pointer in the header to say: the proof rejects a TU that
// gains a warning. The two qualifiers are separate rewrites, nonnull and
// nullable, so a project plan names the qualifier with the parameter.
//
//===----------------------------------------------------------------------===//

#include "CIRModernizeFramework.h"

#include "clang/CIR/Dialect/Analysis/CIRNullabilitySafety.h"

using namespace clang;
using namespace clang::cirmodernize;

namespace {

class Nullability : public Rewrite {
public:
  Nullability(Context &Ctx, unsigned Id, bool NonNull)
      : Rewrite(Ctx, Id), NonNull(NonNull) {}

  StringRef name() const override { return NonNull ? "nonnull" : "nullable"; }
  unsigned diagID(DiagnosticsEngine &Diags) const override {
    return Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                 "pointer parameter %0 of %1 can say whether "
                                 "it may be null");
  }
  StringRef whyNeedBlocked() const override { return "nullability: needed"; }

  StringRef qualifier() const { return NonNull ? "_Nonnull" : "_Nullable"; }

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
    unsigned Offset = Context::thisOffset(FD);
    for (unsigned Index = 0; Index < FD->getNumParams(); ++Index) {
      ParmVarDecl *P = FD->getParamDecl(Index);
      if (!P->getType()->isPointerType() || P->getType()->getNullability())
        continue;
      bool IsPlanned = Planned && Planned->contains(Index);
      const cir::NullabilityFact *Fact = nullptr;
      if (It != Facts.end())
        if (auto F = It->second.find(Index + Offset); F != It->second.end())
          Fact = &F->second;
      if (!(Fact && proves(*Fact)) && !IsPlanned)
        continue;
      Ctx.count(NonNull ? "nonnull: proven parameters"
                        : "nullable: proven parameters");
      Candidate Cand{FD, P, Index, Id};
      StringRef Why = FunctionWhy;
      if (Why.empty() && !declEdits(Cand))
        Why = "nullability: declarator is not a plain pointer";
      Ctx.add(std::move(Cand), Why);
    }
  }

  void addExternal(FunctionDecl *FD, unsigned Index) override {
    ParmVarDecl *P = FD->getParamDecl(Index);
    StringRef Why = Ctx.whyIneligible(FD);
    Candidate Cand{FD, P, Index, Id};
    Cand.External = true;
    if (Why.empty() && !declEdits(Cand))
      Why = "nullability: declarator is not a plain pointer";
    Ctx.add(std::move(Cand), Why);
  }

private:
  // A reassigned or address-taken parameter holds other values than the
  // argument, so the facts about it do not describe the argument.
  bool proves(const cir::NullabilityFact &F) const {
    if (F.reassigned || F.addressTaken)
      return false;
    if (NonNull)
      return F.mustDeref;
    return !F.mustDeref && F.nullChecked && !F.derefUnchecked;
  }

  // The qualifier after the '*' of every declaration this TU sees.
  bool declEdits(Candidate &Cand) {
    for (const FunctionDecl *R : Cand.FD->redecls()) {
      std::optional<PointerTypeLoc> Star =
          Context::starOf(R->getParamDecl(Cand.Index));
      if (!Star || Star->getStarLoc().isMacroID())
        return false;
      Cand.Edits.push_back(FixItHint::CreateInsertion(
          Star->getStarLoc().getLocWithOffset(1), (qualifier() + " ").str()));
    }
    return true;
  }

  const bool NonNull;
  llvm::StringMap<llvm::DenseMap<unsigned, cir::NullabilityFact>> Facts;
};

} // namespace

std::unique_ptr<Rewrite> clang::cirmodernize::createNonNull(Context &Ctx,
                                                            unsigned Id) {
  return std::make_unique<Nullability>(Ctx, Id, /*NonNull=*/true);
}

std::unique_ptr<Rewrite> clang::cirmodernize::createNullable(Context &Ctx,
                                                             unsigned Id) {
  return std::make_unique<Nullability>(Ctx, Id, /*NonNull=*/false);
}
