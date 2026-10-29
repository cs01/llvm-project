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
#include "clang/AST/RecordLayout.h"
#include "clang/AST/VTableBuilder.h"
#include "clang/Basic/CharInfo.h"
#include "clang/CIR/Dialect/Analysis/CIRPointeeWrites.h"
#include "clang/Lex/Lexer.h"

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
    if (isa<CXXConstructorDecl, CXXDestructorDecl>(FD))
      return;
    // A function template, or a member of a class template, is proven by
    // every instantiation CIR emitted.
    // Callers need no edit: a T * argument converts to const T *.
    std::optional<llvm::SmallVector<FunctionDecl *>> Insts;
    if (FD->isTemplated()) {
      Insts = Ctx.instantiationsOf(FD);
      if (!Insts)
        return;
    }
    bool Template = Insts.has_value();
    llvm::SmallVector<const llvm::DenseMap<unsigned, cir::PointeeWriteFact> *>
        Proofs;
    if (!Template) {
      if (auto It = Facts.find(Ctx.mangle(FD)); It != Facts.end())
        Proofs.push_back(&It->second);
    } else {
      for (FunctionDecl *Spec : *Insts)
        if (auto It = Facts.find(Ctx.mangle(Spec)); It != Facts.end())
          Proofs.push_back(&It->second);
    }
    const llvm::DenseSet<unsigned> *Planned = Ctx.planned(FD, name());
    if (Proofs.empty() && !Planned)
      return;
    StringRef FunctionWhy =
        Ctx.whyIneligible(FD, Context::AllowTemplates | Context::AllowMacro |
                                  Context::AllowFnPtrSlots);
    unsigned Offset = Context::thisOffset(FD);
    llvm::SmallVector<unsigned> Indices;
    for (const auto *Proof : Proofs)
      for (auto &[ArgIndex, Fact] : *Proof)
        if (ArgIndex >= Offset && ArgIndex - Offset < FD->getNumParams() &&
            FD->getParamDecl(ArgIndex - Offset)->getType()->isPointerType() &&
            !llvm::is_contained(Indices, ArgIndex - Offset))
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
      for (const auto *Proof : Proofs) {
        auto Fact = Proof->find(Index + Offset);
        if (!Why.empty() || Fact == Proof->end())
          continue;
        if (Fact->second.written)
          Why = Ctx.save("const: " + Fact->second.why);
        for (auto &[Callee, CalleeIndex] : Fact->second.forwards) {
          const FunctionDecl *CFD = Ctx.declByMangledName(Callee);
          // Forwarded into an instantiation: the template's parameter is
          // the one that has to become const.
          if (CFD && CFD->isTemplateInstantiation())
            CFD = CFD->getTemplateInstantiationPattern();
          unsigned CalleeOffset = CFD ? Context::thisOffset(CFD) : 0;
          if (CFD && CalleeIndex < CalleeOffset) {
            if (Why.empty())
              Why = "const: passed as this to a non-const method";
            continue;
          }
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
      if (Why.empty())
        Why = slotEdits(Cand);
      if (Why.empty() && !declEdits(Cand))
        Why = "const: pointer spelled through a typedef or macro";
      Ctx.add(std::move(Cand), Why);
    }
  }

  void addExternal(FunctionDecl *FD, unsigned Index) override {
    ParmVarDecl *P = FD->getParamDecl(Index);
    StringRef Why = whyParamIneligible(P);
    if (Why.empty())
      Why =
          Ctx.whyIneligible(FD, Context::AllowTemplates | Context::AllowMacro |
                                    Context::AllowFnPtrSlots);
    Candidate Cand{FD, P, Index, Id};
    Cand.External = true;
    // This TU may be the one that stores the function in a callback slot.
    if (Why.empty())
      Why = slotEdits(Cand);
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
      const ParmVarDecl *P = R->getParamDecl(Cand.Index);
      std::optional<PointerTypeLoc> Star = Context::starOf(P);
      if (!Star) {
        if (!typedefEdit(Cand, P))
          return false;
        continue;
      }
      if (!editableIn(Star->getStarLoc(), R))
        return false;
      // A pointee that is itself a pointer takes const after its own '*'
      // (char *const *); in front, const would qualify what that points to.
      // The inner pointer may be qualified itself (T *restrict *p).
      if (auto Inner = Star->getPointeeLoc()
                           .getUnqualifiedLoc()
                           .getAs<PointerTypeLoc>()) {
        SourceLocation InnerStar = Inner.getStarLoc();
        if (InnerStar.isInvalid() || !editableIn(InnerStar, R))
          return false;
        Cand.Edits.push_back(FixItHint::CreateInsertion(
            Ctx.SM.getSpellingLoc(InnerStar).getLocWithOffset(1), "const "));
        continue;
      }
      SourceLocation Begin = Star->getPointeeLoc().getBeginLoc();
      // A pointee from a macro argument (T *p in a macro taking T) would put
      // const into the argument, which the macro may paste elsewhere; the
      // star is in the macro itself, so const goes before it: T const *p.
      if (Begin.isMacroID() && Ctx.SM.isMacroArgExpansion(Begin)) {
        SourceLocation StarLoc = Star->getStarLoc();
        if (Ctx.SM.isMacroArgExpansion(StarLoc) || !editableIn(StarLoc, R))
          return false;
        // `T_*p` must not become `T_const *p`.
        SourceLocation At = Ctx.SM.getSpellingLoc(StarLoc);
        bool Spaced =
            isWhitespace(*Ctx.SM.getCharacterData(At.getLocWithOffset(-1)));
        Cand.Edits.push_back(
            FixItHint::CreateInsertion(At, Spaced ? "const " : " const "));
        continue;
      }
      if (Begin.isInvalid() || !editableIn(Begin, R))
        return false;
      Cand.Edits.push_back(
          FixItHint::CreateInsertion(Ctx.SM.getSpellingLoc(Begin), "const "));
    }
    return true;
  }
  // A function stored in a callback slot (a function-pointer typedef): the
  // typedef's parameter becomes const too, and every function stored in that
  // slot type must, which the shared edit and the needs tie together.
  StringRef slotEdits(Candidate &Cand) {
    llvm::SmallVector<const TypedefNameDecl *> Slots;
    if (!Ctx.fnPtrSlots(Cand.FD, Slots))
      return {};
    for (const TypedefNameDecl *TD : Slots) {
      TypeLoc TL = TD->getTypeSourceInfo()->getTypeLoc();
      if (auto PTL = TL.getAs<PointerTypeLoc>())
        TL = PTL.getPointeeLoc();
      auto FPTL = TL.IgnoreParens().getAs<FunctionProtoTypeLoc>();
      if (!FPTL || Cand.Index >= FPTL.getNumParams() ||
          !FPTL.getParam(Cand.Index))
        return "const: a callback typedef it is stored as is not plain";
      std::optional<PointerTypeLoc> Star =
          Context::starOf(FPTL.getParam(Cand.Index));
      if (!Star || Star->getPointeeLoc().getType()->isPointerType())
        return "const: a callback typedef it is stored as is not plain";
      if (!Star->getPointeeLoc().getType().isConstQualified()) {
        SourceLocation Begin = Context::slotSpot(TD, Cand.Index);
        if (Begin.isInvalid())
          return "const: a callback typedef it is stored as is in a macro";
        Cand.Edits.push_back(FixItHint::CreateInsertion(Begin, "const "));
      }
      for (const FunctionDecl *Other : Ctx.functionsInSlot(TD))
        if (Other != Cand.FD->getCanonicalDecl()) {
          ParmVarDecl *Needed = Context::keyParam(Other, Cand.Index);
          if (!Needed)
            return "const: a callback typedef it is stored as is not plain";
          Cand.Needs.push_back(Needed);
        }
    }
    return {};
  }

  // A token of declaration R that an edit can change: written in the file,
  // or inside the macro that writes R itself (its name comes from the same
  // expansion). A type macro shared by other declarations is not edited.
  bool editableIn(SourceLocation Loc, const FunctionDecl *R) {
    if (!Loc.isMacroID())
      return true;
    const SourceManager &SM = Ctx.SM;
    SourceLocation Name = R->getLocation();
    SourceLocation Spelling = SM.getSpellingLoc(Loc);
    return Ctx.projectMode() && Name.isMacroID() &&
           SM.getExpansionLoc(Loc) == SM.getExpansionLoc(Name) &&
           !SM.isWrittenInScratchSpace(Spelling) &&
           !SM.isInSystemHeader(Spelling);
  }

  // A pointer named by a typedef (XPtr p) is spelled out: const X *p.
  bool typedefEdit(Candidate &Cand, const ParmVarDecl *P) {
    TypeSourceInfo *TSI = P->getTypeSourceInfo();
    if (!TSI || P->getType().hasLocalQualifiers())
      return false;
    auto TTL = TSI->getTypeLoc().getAs<TypedefTypeLoc>();
    QualType Pointee = P->getType()->getPointeeType();
    if (!TTL || Pointee.isNull() || Pointee->isPointerType() ||
        Pointee->isFunctionType() || Pointee->isArrayType())
      return false;
    SourceLocation Begin = TTL.getBeginLoc(), End = TTL.getEndLoc();
    if (Begin.isMacroID() || End.isMacroID())
      return false;
    const LangOptions &LO = Ctx.C.getLangOpts();
    End = Lexer::getLocForEndOfToken(End, 0, Ctx.SM, LO);
    while (isHorizontalWhitespace(*Ctx.SM.getCharacterData(End)))
      End = End.getLocWithOffset(1);
    PrintingPolicy PP(LO);
    Cand.Edits.push_back(FixItHint::CreateReplacement(
        CharSourceRange::getCharRange(Begin, End),
        "const " + Pointee.getAsString(PP) + " *"));
    return true;
  }

  // Whether a callee only reads through its CIR argument Index. CIR keeps no
  // const qualifiers, so the answer comes from the callee's declaration.
  cir::PointerArgUse argUse(cir::FuncOp Callee, unsigned Index) {
    const FunctionDecl *FD = Ctx.declByMangledName(Callee.getSymName());
    if (!FD)
      return cir::PointerArgUse::Writes;
    unsigned Offset = Context::thisOffset(FD);
    // A non-const method may still become const itself (const-method).
    if (Index < Offset)
      return cast<CXXMethodDecl>(FD)->isConst() ? cir::PointerArgUse::ReadOnly
                                                : cir::PointerArgUse::Forward;
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
        MD->getParent()->isLambda())
      return;
    // A member of a class template is proven by every instantiation.
    llvm::SmallVector<const FunctionDecl *> Proven{FD};
    if (MD->isTemplated()) {
      auto Insts = Ctx.instantiationsOf(FD);
      if (!Insts || MD->isVirtual())
        return;
      Proven.assign(Insts->begin(), Insts->end());
    }
    bool Virtual = MD->isVirtual();
    const llvm::DenseSet<unsigned> *Planned = Ctx.planned(FD, name());
    bool AnyFact = false;
    StringRef Why;
    llvm::SmallVector<const FunctionDecl *> Needs;
    for (const FunctionDecl *Inst : Proven) {
      auto It = Facts.find(Ctx.mangle(Inst));
      if (It == Facts.end())
        continue;
      auto Fact = It->second.find(0);
      if (Fact == It->second.end())
        continue;
      AnyFact = true;
      if (Fact->second.written && Why.empty())
        Why = Ctx.save("const-method: " + Fact->second.why);
      // `this` passed on as the `this` of another method: right once that
      // method is const too (the template's, for an instantiation).
      for (auto &[Callee, CalleeIndex] : Fact->second.forwards) {
        const FunctionDecl *CFD = Ctx.declByMangledName(Callee);
        StringRef Name = Callee;
        uint64_t Slot;
        if (Name.consume_front(cir::VirtualSlotPrefix) &&
            !Name.getAsInteger(10, Slot))
          CFD = virtualAt(cast<CXXMethodDecl>(Inst), Slot);
        if (CFD && CFD->isTemplateInstantiation())
          CFD = CFD->getTemplateInstantiationPattern();
        if (CFD && CalleeIndex == 0 && Context::thisOffset(CFD))
          Needs.push_back(CFD);
        else if (Why.empty())
          Why = "const-method: this passed to a non-const pointer parameter";
      }
    }
    bool Proven0 = AnyFact;
    if (!Proven0 && !(Planned && Planned->contains(0)))
      return;
    Ctx.count("const-method: methods");
    if (Why.empty())
      Why = Ctx.whyIneligible(FD, Virtual ? Context::AllowVirtual
                                          : Context::AllowTemplates);
    if (Virtual)
      Why = overrideSet(MD, Needs, Why);
    add(MD, Why, /*External=*/false, Needs);
  }

  void addExternal(FunctionDecl *FD, unsigned) override {
    auto *MD = dyn_cast<CXXMethodDecl>(FD);
    if (!MD || !MD->isInstance() || MD->isConst() ||
        !GroupAdded.insert(MD->getCanonicalDecl()).second)
      return;
    if (!MD->isVirtual())
      return add(MD, Ctx.whyIneligible(FD), /*External=*/true);
    llvm::SmallVector<const FunctionDecl *> Needs;
    StringRef Why =
        overrideSet(MD, Needs, Ctx.whyIneligible(FD, Context::AllowVirtual));
    add(MD, Why, /*External=*/true, Needs);
  }

private:
  // The virtual method in vtable slot Slot of MD's class: declared there or
  // in its primary bases, which share that vtable. (A call through another
  // base adjusts `this` first, so it never reaches here.)
  const CXXMethodDecl *virtualAt(const CXXMethodDecl *MD, uint64_t Slot) {
    auto *VTC = dyn_cast<ItaniumVTableContext>(Ctx.C.getVTableContext());
    if (!VTC || MD->getParent()->isDependentContext())
      return nullptr;
    for (const CXXRecordDecl *RD = MD->getParent(); RD;
         RD = Ctx.C.getASTRecordLayout(RD).getPrimaryBase())
      for (const CXXMethodDecl *M : RD->methods())
        if (M->isVirtual() && !isa<CXXDestructorDecl>(M) &&
            VTC->getMethodVTableIndex(GlobalDecl(M)) == Slot)
          return M;
    return nullptr;
  }

  // Every method this one overrides or is overridden by, as far as this
  // translation unit sees: they stay overrides of each other only if all of
  // them become const. Each needs all the others, so project mode joins the
  // sets that different translation units see. A member with no body here
  // gets a candidate too: a pure virtual one has nothing to prove, and one
  // defined elsewhere is proven by its own translation unit.
  StringRef overrideSet(CXXMethodDecl *MD,
                        llvm::SmallVectorImpl<const FunctionDecl *> &Needs,
                        StringRef Why) {
    llvm::SetVector<const CXXMethodDecl *> Set;
    Set.insert(MD->getCanonicalDecl());
    for (unsigned I = 0; I < Set.size(); ++I) {
      for (const CXXMethodDecl *O : Set[I]->overridden_methods())
        Set.insert(O->getCanonicalDecl());
      for (CXXRecordDecl *RD : Ctx.Visitor.Records)
        for (CXXMethodDecl *M : RD->methods())
          if (llvm::is_contained(M->overridden_methods(), Set[I]))
            Set.insert(M->getCanonicalDecl());
    }
    for (const CXXMethodDecl *M : Set) {
      if (M == MD->getCanonicalDecl())
        continue;
      if (M->isConst() || M->isDependentContext())
        return "const-method: an override in its set cannot change";
      Needs.push_back(M);
      const FunctionDecl *Def = M->getDefinition();
      if (Def && Def->hasBody())
        continue; // addDefined proposes it.
      if (!GroupAdded.insert(M).second)
        continue;
      auto *Member = const_cast<CXXMethodDecl *>(M);
      llvm::SmallVector<const FunctionDecl *> MemberNeeds;
      StringRef MemberWhy =
          overrideSet(Member, MemberNeeds,
                      Ctx.whyIneligible(Member, Context::AllowVirtual));
      add(Member, MemberWhy, /*External=*/!M->isPureVirtual(), MemberNeeds);
    }
    return Why;
  }

  llvm::DenseSet<const CXXMethodDecl *> GroupAdded;

  void add(CXXMethodDecl *MD, StringRef Why, bool External,
           ArrayRef<const FunctionDecl *> Needs = {}) {
    Candidate Cand{MD, nullptr, 0, Id};
    Cand.External = External;
    Cand.NeedsMethods.assign(Needs.begin(), Needs.end());
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
