//===--- CIRModernize.cpp - CIR-proven idiomatic C++ rewrites -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Runs every enabled rewrite (ParamToReference.cpp, PointerToConst.cpp) over a
// translation unit: each analyzes the CIR module and proposes candidates on
// the AST; candidates whose needs are blocked are blocked in turn until
// nothing changes; the rest are reported as warnings with fix-its, applied,
// or, in project mode, exported with the blocked ones for project.ts.
//
//===----------------------------------------------------------------------===//

#include "CIRModernizeFramework.h"

#include "clang/AST/StmtCXX.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/UnifiedSymbolResolution/USRGeneration.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/IOSandbox.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::cirmodernize;

#define DEBUG_TYPE "cir-modernize"

Context::Context(ASTContext &C, const CIRModernizeOptions &Options)
    : C(C), SM(C.getSourceManager()), Options(Options) {}

bool Context::editable(SourceLocation Loc) const {
  if (Loc.isInvalid() || Loc.isMacroID())
    return false;
  return projectMode() ? !SM.isInSystemHeader(Loc) : SM.isInMainFile(Loc);
}

std::string Context::mangle(GlobalDecl GD) {
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

std::string Context::usr(const FunctionDecl *FD) {
  llvm::SmallString<128> Buf;
  if (index::generateUSRForDecl(FD->getCanonicalDecl(), Buf))
    return {};
  return std::string(Buf);
}

const FunctionDecl *Context::declByMangledName(StringRef Mangled) {
  if (!MangledIndexed) {
    MangledIndexed = true;
    // The visitor skips implicit code, but CIR calls the special members
    // Sema declared implicitly, such as a defaulted copy assignment.
    llvm::SetVector<FunctionDecl *> Decls(Visitor.Declared.begin(),
                                          Visitor.Declared.end());
    for (CXXRecordDecl *RD : Visitor.Records)
      for (Decl *D : RD->decls())
        if (auto *M = dyn_cast<CXXMethodDecl>(D); M && M->isImplicit())
          Decls.insert(M);
    for (FunctionDecl *FD : Decls) {
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
  }
  return ByMangled.lookup(Mangled);
}

void Context::indexCallees() {
  CalleesIndexed = true;
  for (CallExpr *CE : Visitor.Calls) {
    const Expr *Callee = CE->getCallee()->IgnoreParenImpCasts();
    if (auto *DRE = dyn_cast<DeclRefExpr>(Callee))
      CalleeRefs.insert(DRE);
    else if (auto *ME = dyn_cast<MemberExpr>(Callee))
      CalleeRefs.insert(ME);
  }
}

// __PRETTY_FUNCTION__ and friends spell the parameter types into a string,
// so changing a parameter type changes program output. Lambdas are children
// of their enclosing body, so their uses count too. std::source_location
// (logging macros) arrives through a default argument or member initializer,
// which children() visits only when Sema rewrote it per use (C++20).
static bool spellsSignature(const Stmt *S) {
  if (!S)
    return false;
  if (auto *PE = dyn_cast<PredefinedExpr>(S))
    switch (PE->getIdentKind()) {
    case PredefinedIdentKind::Func:
    case PredefinedIdentKind::Function:
      break;
    default:
      return true;
    }
  if (auto *SL = dyn_cast<SourceLocExpr>(S))
    return SL->getIdentKind() == SourceLocIdentKind::FuncSig ||
           SL->getIdentKind() == SourceLocIdentKind::SourceLocStruct;
  if (auto *DA = dyn_cast<CXXDefaultArgExpr>(S))
    return spellsSignature(DA->getExpr());
  if (auto *DI = dyn_cast<CXXDefaultInitExpr>(S))
    return spellsSignature(DI->getExpr());
  for (const Stmt *Child : S->children())
    if (spellsSignature(Child))
      return true;
  return false;
}

StringRef Context::whyIneligible(FunctionDecl *FD) {
  if (!CalleesIndexed)
    indexCallees();
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
  if (const FunctionDecl *Def = FD->getDefinition();
      Def && spellsSignature(Def->getBody()))
    return "signature spelled in __PRETTY_FUNCTION__";
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

// A C file compiled as C++ (Bitcoin #includes ctaes.c) proves fine, but a
// C++-only edit breaks it for every C compiler and C caller, which no
// translation unit in the project shows.
StringRef Context::whyVisibleToC(const FunctionDecl *FD) const {
  if (FD->isExternC())
    return "C language linkage";
  for (const FunctionDecl *R : FD->redecls()) {
    StringRef Ext = llvm::sys::path::extension(SM.getFilename(R->getLocation()));
    if (Ext == ".c" || Ext == ".m")
      return "declared in a C source file";
  }
  return {};
}

const llvm::DenseSet<unsigned> *Context::planned(const FunctionDecl *FD,
                                                 StringRef Rewrite) {
  auto P = Plan.find(Rewrite);
  if (P == Plan.end())
    return nullptr;
  auto It = P->second.find(usr(FD));
  return It == P->second.end() ? nullptr : &It->second;
}

void Context::add(Candidate Cand, StringRef Why) {
  if (!Why.empty()) {
    count(Why);
    // A project-wide rewrite needs to hear why this translation unit blocks
    // a function, not just that it found no candidate.
    if (!projectMode())
      return;
    Cand.Live = false;
    Cand.Why = Why;
  }
  // A candidate about `this` has no parameter, and nothing needs it.
  if (Cand.Param)
    ByParam[{Cand.Param, Cand.Rewrite}] = Candidates.size();
  Candidates.push_back(std::move(Cand));
}

bool Context::live(const ParmVarDecl *P, unsigned Rewrite) const {
  auto It = ByParam.find({P, Rewrite});
  return It != ByParam.end() && Candidates[It->second].Live;
}

ParmVarDecl *Context::keyParam(const FunctionDecl *FD, unsigned Index) {
  const FunctionDecl *Def = FD->getDefinition();
  const FunctionDecl *Key =
      Def && Def->hasBody() ? Def : FD->getCanonicalDecl();
  return Index < Key->getNumParams()
             ? const_cast<ParmVarDecl *>(Key->getParamDecl(Index))
             : nullptr;
}

unsigned Context::thisOffset(const FunctionDecl *FD) {
  const auto *MD = dyn_cast<CXXMethodDecl>(FD);
  return MD && MD->isInstance() ? 1 : 0;
}

std::optional<PointerTypeLoc> Context::starOf(const ParmVarDecl *P) {
  TypeSourceInfo *TSI = P->getTypeSourceInfo();
  if (!TSI)
    return std::nullopt;
  auto PTL = TSI->getTypeLoc().getUnqualifiedLoc().getAs<PointerTypeLoc>();
  if (!PTL)
    return std::nullopt;
  return PTL;
}

namespace {

using Factory = std::unique_ptr<Rewrite> (*)(Context &, unsigned);
struct RewriteInfo {
  StringRef Name;
  // "identical" rewrites leave the compiled code unchanged and are proven by
  // IR equality; "refinement" ones may change it (added checks, cleanups).
  StringRef Group;
  bool Default;
  Factory Create;
};
// nonnull and nullable are clang extensions, and would annotate the
// parameters ref turns into references: only when asked for.
constexpr RewriteInfo AllRewrites[] = {
    {"ref", "identical", true, createParamToReference},
    {"const", "identical", true, createPointerToConst},
    {"const-method", "identical", true, createConstMethod},
    {"nonnull", "identical", false, createNonNull},
    {"nullable", "identical", false, createNullable},
    {"lifetimebound", "identical", true, createLifetimebound},
    {"lifetimebound-this", "identical", true, createLifetimeboundThis},
    {"capture-by", "identical", true, createCaptureBy},
};
constexpr StringRef Groups[] = {"identical", "refinement"};

class Modernizer {
public:
  Modernizer(ASTContext &C, const CIRModernizeOptions &Options)
      : Ctx(C, Options) {}

  void run(mlir::ModuleOp Module, DiagnosticsEngine &Diags) {
    HasFacts = static_cast<bool>(Module);
    if (!enableRewrites(Diags))
      return;
    if (!Ctx.Options.PlanPath.empty() && !loadPlan(Diags))
      return;
    Ctx.Visitor.TraverseDecl(Ctx.C.getTranslationUnitDecl());
    // Without a module (CIRGen failed) nothing is proven here; candidates
    // come only from the plan.
    if (Module)
      for (auto &R : Rewrites)
        R->analyze(Module);
    for (FunctionDecl *FD : Ctx.Visitor.Functions)
      for (auto &R : Rewrites)
        R->addDefined(FD);
    for (FunctionDecl *FD : Ctx.Visitor.Declared) {
      if (FD->getDefinition() && FD->getDefinition()->hasBody())
        continue;
      for (auto &R : Rewrites)
        if (const llvm::DenseSet<unsigned> *Planned =
                Ctx.planned(FD, R->name()))
          for (unsigned Index : *Planned)
            if (Index < FD->getNumParams())
              R->addExternal(FD, Index);
    }
    resolve();
    if (Ctx.projectMode())
      exportCandidates(Diags);
    else
      report(Diags);
    LLVM_DEBUG({
      Ctx.drops()["rewritten"] = llvm::count_if(
          Ctx.Candidates, [](const Candidate &Cand) { return Cand.Live; });
      for (const auto &[Why, N] : Ctx.drops())
        llvm::dbgs() << "cir-modernize: " << N << " " << Why << "\n";
    });
  }

private:
  bool enableRewrites(DiagnosticsEngine &Diags) {
    llvm::SmallVector<StringRef> Only, Skip;
    StringRef(Ctx.Options.Only).split(Only, ',', -1, /*KeepEmpty=*/false);
    StringRef(Ctx.Options.Skip).split(Skip, ',', -1, /*KeepEmpty=*/false);
    for (StringRef Name : llvm::concat<StringRef>(Only, Skip))
      if (!llvm::is_contained(Groups, Name) &&
          !llvm::any_of(AllRewrites,
                        [&](const RewriteInfo &R) { return R.Name == Name; })) {
        Diags.Report(Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                           "unknown modernize rewrite '%0'"))
            << Name;
        return false;
      }
    auto Names = [](ArrayRef<StringRef> List, const RewriteInfo &R) {
      return llvm::is_contained(List, R.Name) ||
             llvm::is_contained(List, R.Group);
    };
    for (const RewriteInfo &R : AllRewrites)
      if ((Only.empty() ? R.Default : Names(Only, R)) && !Names(Skip, R))
        Rewrites.push_back(R.Create(Ctx, Rewrites.size()));
    return true;
  }

  void resolve() {
    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (Candidate &Cand : Ctx.Candidates) {
        if (!Cand.Live)
          continue;
        Rewrite &R = *Rewrites[Cand.Rewrite];
        StringRef Why;
        if (!llvm::all_of(Cand.Needs, [&](const ParmVarDecl *P) {
              return Ctx.live(P, Cand.Rewrite);
            }))
          Why = R.whyNeedBlocked();
        else
          Why = R.finalize(Cand);
        if (Why.empty())
          Why = whyConditional(Cand);
        if (!Why.empty()) {
          Ctx.count(Why);
          Cand.Live = false;
          Cand.Why = Why;
          Changed = true;
        }
      }
    }
  }

  // The facts behind a candidate hold for this build configuration only.
  StringRef whyConditional(const Candidate &Cand) {
    const ConditionalRecord *CR = Ctx.Options.Conditionals;
    if (!CR)
      return {};
    // The body is what the facts come from; another translation unit proved
    // an external candidate's.
    if (!Cand.External)
      if (const FunctionDecl *Def = Cand.FD->getDefinition())
        if (CR->overlaps(
                Ctx.SM.getExpansionRange(Def->getSourceRange()).getAsRange()))
          return "defined in or across an #if region";
    for (const auto *Edits : {&Cand.Edits, &Cand.LateEdits})
      for (const FixItHint &Hint : *Edits)
        if (CR->overlaps(Hint.RemoveRange.getAsRange()))
          return "an edit is inside an #if region";
    // A skipped branch may call the function, and nothing here edits that
    // call. Raw tokens give names only, so an operator or conversion, called
    // without one, is blocked by any skipped branch.
    const IdentifierInfo *II = Cand.FD->getIdentifier();
    if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(Cand.FD))
      II = Ctor->getParent()->getIdentifier();
    if (Rewrites[Cand.Rewrite]->changesType() &&
        (II ? CR->skippedNames().contains(II->getName()) : CR->skippedAny()))
      return "named in an #if branch this build skips";
    return {};
  }

  void report(DiagnosticsEngine &Diags) {
    llvm::SmallVector<unsigned> IDs;
    for (auto &R : Rewrites)
      IDs.push_back(R->diagID(Diags));
    Rewriter RW(Ctx.SM, Ctx.C.getLangOpts());
    for (const Candidate &Cand : Ctx.Candidates) {
      if (!Cand.Live)
        continue;
      auto Diag = Diags.Report(Cand.Param ? Cand.Param->getLocation()
                                          : Cand.FD->getLocation(),
                               IDs[Cand.Rewrite]);
      if (Cand.Param)
        Diag << Cand.Param;
      else
        Diag << Cand.FD;
      Diag << Cand.FD;
      for (const auto *Edits : {&Cand.Edits, &Cand.LateEdits})
        for (const FixItHint &Hint : *Edits)
          Diag << Hint;
      if (!Ctx.Options.Apply)
        continue;
      for (const auto *Edits : {&Cand.Edits, &Cand.LateEdits})
        for (const FixItHint &Hint : *Edits) {
          SourceLocation Begin = Hint.RemoveRange.getBegin();
          if (Begin == Hint.RemoveRange.getEnd())
            RW.InsertTextAfter(Begin, Hint.CodeToInsert);
          else
            RW.ReplaceText(Hint.RemoveRange, Hint.CodeToInsert);
        }
    }
    if (Ctx.Options.Apply)
      RW.overwriteChangedFiles();
  }

  // {"functions": [{"usr": "...", "index": N, "kind": "ref"}, ...]}
  bool loadPlan(DiagnosticsEngine &Diags) {
    auto Fail = [&](StringRef Why) {
      Diags.Report(Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                         "cannot read modernize plan %0: %1"))
          << Ctx.Options.PlanPath << Why;
      return false;
    };
    // The plan and the export are side files of a project-wide rewrite, not
    // compiler inputs or outputs, like the files -fcir-modernize-apply writes.
    auto BypassSandbox = llvm::sys::sandbox::scopedDisable();
    auto Buf = llvm::MemoryBuffer::getFile(Ctx.Options.PlanPath);
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
      StringRef Kind = E ? E->getString("kind").value_or("ref") : "";
      if (!USR || !Index || *Index < 0 ||
          !llvm::any_of(
              AllRewrites,
              [&](const RewriteInfo &R) { return R.Name == Kind; }))
        return Fail("bad function entry");
      Ctx.plan(Kind, *USR, *Index);
    }
    return true;
  }

  void exportEdit(llvm::json::OStream &J, const FixItHint &Hint) {
    auto [FID, Begin] = Ctx.SM.getDecomposedLoc(Hint.RemoveRange.getBegin());
    unsigned End = Ctx.SM.getFileOffset(Hint.RemoveRange.getEnd());
    OptionalFileEntryRef FE = Ctx.SM.getFileEntryRefForID(FID);
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
    llvm::raw_fd_ostream OS(Ctx.Options.ExportPath, EC);
    if (EC) {
      Diags.Report(Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                         "cannot write %0: %1"))
          << Ctx.Options.ExportPath << EC.message();
      return;
    }
    llvm::json::OStream J(OS, 1);
    J.object([&] {
      J.attribute("facts", HasFacts);
      J.attributeArray("candidates", [&] {
        for (const Candidate &Cand : Ctx.Candidates)
          J.object([&] {
            StringRef Kind = Rewrites[Cand.Rewrite]->name();
            J.attribute("usr", Ctx.usr(Cand.FD));
            J.attribute("index", Cand.Index);
            J.attribute("kind", Kind);
            J.attribute("function", Cand.FD->getQualifiedNameAsString());
            J.attribute("param", Cand.Param ? Cand.Param->getName() : "this");
            J.attribute("defined", !Cand.External);
            J.attribute("internal", !Cand.FD->isExternallyVisible());
            J.attribute("method", isa<CXXMethodDecl>(Cand.FD));
            J.attribute("changesType", Rewrites[Cand.Rewrite]->changesType());
            J.attribute("location", (Cand.Param ? Cand.Param->getLocation()
                                                : Cand.FD->getLocation())
                                        .printToString(Ctx.SM));
            if (!Cand.Live) {
              J.attribute("blocked", Cand.Why);
              return;
            }
            J.attributeArray("edits", [&] {
              for (const auto *Edits : {&Cand.Edits, &Cand.LateEdits})
                for (const FixItHint &Hint : *Edits)
                  exportEdit(J, Hint);
            });
            J.attributeArray("needs", [&] {
              for (const auto *Needs : {&Cand.Needs, &Cand.LateNeeds})
                for (const ParmVarDecl *P : *Needs)
                  J.object([&] {
                    J.attribute("usr", Ctx.usr(cast<FunctionDecl>(
                                           P->getDeclContext())));
                    J.attribute("index", P->getFunctionScopeIndex());
                    J.attribute("kind", Kind);
                  });
            });
          });
      });
      if (const ConditionalRecord *CR = Ctx.Options.Conditionals)
        J.attributeArray("skippedNames", [&] {
          for (const auto &E : CR->skippedNames())
            J.value(E.getKey());
        });
      exportInventory(J, Ctx);
    });
  }

  Context Ctx;
  std::vector<std::unique_ptr<Rewrite>> Rewrites;
  bool HasFacts = false;
};

} // namespace

void clang::runCIRModernize(mlir::ModuleOp Module, ASTContext &C,
                            DiagnosticsEngine &Diags,
                            const CIRModernizeOptions &Options) {
  Modernizer(C, Options).run(Module, Diags);
}

void ConditionalRecord::Endif(SourceLocation Loc, SourceLocation IfLoc) {
  if (Open.empty())
    return;
  Regions.push_back({Open.back(), Loc});
  Open.pop_back();
}

void ConditionalRecord::SourceRangeSkipped(SourceRange Range,
                                           SourceLocation EndifLoc) {
  SourceManager &SM = PP.getSourceManager();
  SourceLocation Begin = Range.getBegin();
  if (Begin.isInvalid() || SM.isInSystemHeader(Begin))
    return;
  auto [FID, Offset] = SM.getDecomposedLoc(Begin);
  unsigned End = SM.getFileOffset(Range.getEnd());
  bool Invalid = false;
  StringRef Buf = SM.getBufferData(FID, &Invalid);
  if (Invalid)
    return;
  Lexer L(SM.getLocForStartOfFile(FID), PP.getLangOpts(), Buf.begin(),
          Buf.begin() + Offset, Buf.end());
  Token T;
  while (!L.LexFromRawLexer(T) && SM.getFileOffset(T.getLocation()) <= End)
    if (T.is(tok::raw_identifier))
      Skipped.insert(T.getRawIdentifier());
}

bool ConditionalRecord::overlaps(SourceRange R) const {
  SourceManager &SM = PP.getSourceManager();
  if (R.isInvalid())
    return false;
  auto [FID, Begin] = SM.getDecomposedLoc(SM.getFileLoc(R.getBegin()));
  auto [EndFID, End] = SM.getDecomposedLoc(SM.getFileLoc(R.getEnd()));
  if (EndFID != FID)
    End = Begin;
  // Clang treats a header as guarded only when nothing but its outermost
  // #ifndef surrounds the code, so that region is the guard: the first one.
  std::optional<unsigned> Guard;
  OptionalFileEntryRef FE = SM.getFileEntryRefForID(FID);
  if (FE && PP.getHeaderSearchInfo().isFileMultipleIncludeGuarded(*FE))
    for (SourceRange Region : Regions) {
      auto [RFID, RBegin] = SM.getDecomposedLoc(Region.getBegin());
      if (RFID == FID && (!Guard || RBegin < *Guard))
        Guard = RBegin;
    }
  for (SourceRange Region : Regions) {
    auto [RFID, RBegin] = SM.getDecomposedLoc(Region.getBegin());
    if (RFID != FID || RBegin == Guard)
      continue;
    unsigned REnd = SM.getFileOffset(Region.getEnd());
    if (RBegin <= End && Begin <= REnd)
      return true;
  }
  return false;
}
