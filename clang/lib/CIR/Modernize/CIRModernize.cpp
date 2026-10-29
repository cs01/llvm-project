//===--- CIRModernize.cpp - CIR-proven idiomatic C++ rewrites -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The driver for -fcir-modernize, which rewrites C++ source into safer,
// more idiomatic forms (a pointer parameter that is never null becomes a
// reference, a pointer that is never written through becomes a pointer to
// const). The design, the list of rewrites and how each is proven are in
// clang/utils/cirmodernize/README.md and docs/cir-modernize-design.md.
//
// It runs inside one clang invocation, after CIRGen has built the CIR module
// and while the AST is still alive (called from CIRGenConsumer in
// CIRGenAction.cpp):
//
//  1. Analyze. Each enabled rewrite (one per file: ParamToReference.cpp,
//     PointerToConst.cpp, ...) runs its analysis on the CIR module and
//     records facts keyed by mangled function name.
//  2. Propose. The rewrite finds the matching FunctionDecl by mangling AST
//     declarations the same way (Context::declByMangledName), checks what
//     only the AST can tell (macros, #if regions, overloads, address taken)
//     and builds a candidate with the source edits it needs.
//  3. Settle. A candidate can depend on others, for example a parameter
//     forwarded to another candidate's parameter. Candidates whose
//     dependencies are blocked are blocked in turn, until nothing changes.
//  4. Report. The survivors become warnings carrying fix-its. With
//     -fcir-modernize-apply the edits are written to the files. In project
//     mode, survivors and blocked candidates are exported for project.ts,
//     which merges them across translation units.
//
// The analyses that propose an edit cannot be trusted to vouch for it, so the
// edits are checked outside this file, with classic codegen and no CIR:
// compile the translation unit before and after the edits and compare the
// LLVM IR with cir-modernize-ircmp (clang/tools/cir-modernize-ircmp), which
// exits 0 when the modules match apart from mangled names and reference
// attributes:
//
//   clang++ <flags> -emit-llvm -c -O0 -g0 foo.cpp -o before.bc
//   clang++ <flags> -fclangir -emit-cir -fcir-modernize \
//       -fcir-modernize-apply foo.cpp -o /dev/null
//   clang++ <flags> -emit-llvm -c -O0 -g0 foo.cpp -o after.bc
//   cir-modernize-ircmp before.bc after.bc
//
// Rewrites that change code on purpose (span, unique_ptr, lock_guard) compile
// with -O1 -Xclang -disable-llvm-passes and compare with
// cir-modernize-ircmp --refine instead.
//
//===----------------------------------------------------------------------===//

#include "CIRModernizeFramework.h"

#include "clang/AST/ParentMapContext.h"
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

#include <set>
#include <tuple>

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
    // CIR calls an instantiation, which the source does not declare.
    for (FunctionDecl *FD : Visitor.Functions)
      if (FD->isTemplated())
        if (auto Insts = instantiationsOf(FD))
          Decls.insert(Insts->begin(), Insts->end());
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

std::optional<llvm::SmallVector<FunctionDecl *>>
Context::instantiationsOf(const FunctionDecl *FD) const {
  llvm::SmallVector<FunctionDecl *> Out;
  auto Take = [&](FunctionDecl *Spec) {
    if (Spec->getTemplateSpecializationKind() != TSK_ImplicitInstantiation)
      return false;
    Out.push_back(Spec);
    return true;
  };
  if (FD->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate) {
    if (FD->getDeclContext()->isDependentContext())
      return std::nullopt;
    for (FunctionDecl *Spec :
         FD->getDescribedFunctionTemplate()->specializations())
      if (!Take(Spec))
        return std::nullopt;
    return Out;
  }
  // A member of a class template written in the template itself (not a
  // partial specialization, not nested in another template).
  const auto *MD = dyn_cast<CXXMethodDecl>(FD);
  const CXXRecordDecl *RD = MD ? MD->getParent() : nullptr;
  const ClassTemplateDecl *CTD = RD ? RD->getDescribedClassTemplate() : nullptr;
  if (!CTD || FD->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      RD->getDeclContext()->isDependentContext())
    return std::nullopt;
  const FunctionDecl *Pattern = FD->getCanonicalDecl();
  for (ClassTemplateSpecializationDecl *Spec : CTD->specializations())
    for (CXXMethodDecl *M : Spec->methods()) {
      const FunctionDecl *From = M->getInstantiatedFromMemberFunction();
      if (From && From->getCanonicalDecl() == Pattern && !Take(M))
        return std::nullopt;
    }
  return Out;
}

// Whether an export macro is spelled in a declaration that starts at Begin
// and names its entity at End. A macro that expands to nothing is not part
// of the declaration's range, so the scan starts two lines up and counts only
// what follows the last `;`, `{` or `}` before Begin.
static bool spellsExportMacro(const SourceManager &SM, const LangOptions &LO,
                              SourceLocation Begin, SourceLocation End,
                              const ConditionalRecord &CR) {
  Begin = SM.getExpansionLoc(Begin);
  End = SM.getExpansionLoc(End);
  auto [FID, At] = SM.getDecomposedLoc(Begin);
  if (FID != SM.getFileID(End))
    return false;
  unsigned To = SM.getFileOffset(End);
  StringRef Buf = SM.getBufferData(FID);
  unsigned From = At;
  for (int Lines = 0; From > 0 && Lines < 3; --From)
    if (Buf[From - 1] == '\n')
      ++Lines;
  Lexer L(SM.getLocForStartOfFile(FID), LO, Buf.begin(), Buf.begin() + From,
          Buf.end());
  Token T;
  bool Found = false;
  while (!L.LexFromRawLexer(T)) {
    unsigned Offset = SM.getFileOffset(T.getLocation());
    if (Offset >= To)
      break;
    if (Offset < At && T.isOneOf(tok::semi, tok::l_brace, tok::r_brace))
      Found = false;
    else if (T.is(tok::raw_identifier) &&
             CR.isExportMacro(T.getRawIdentifier()))
      Found = true;
  }
  return Found;
}

bool Context::exported(const FunctionDecl *FD) const {
  if (!FD->isExternallyVisible())
    return false;
  auto HasAttr = [](const Decl *D) {
    const auto *VA = D->getAttr<VisibilityAttr>();
    return (VA && VA->getVisibility() == VisibilityAttr::Default) ||
           D->hasAttr<DLLExportAttr>() || D->hasAttr<DLLImportAttr>();
  };
  const auto *RD = dyn_cast<CXXRecordDecl>(FD->getDeclContext());
  for (const FunctionDecl *R : FD->redecls())
    if (HasAttr(R))
      return true;
  if (RD && HasAttr(RD))
    return true;
  const ConditionalRecord *CR = Options.Conditionals;
  if (!CR)
    return false;
  const LangOptions &LO = C.getLangOpts();
  for (const FunctionDecl *R : FD->redecls())
    if (spellsExportMacro(SM, LO, R->getBeginLoc(), R->getLocation(), *CR))
      return true;
  return RD &&
         spellsExportMacro(SM, LO, RD->getBeginLoc(), RD->getLocation(), *CR);
}

// The type a converted function pointer is stored as, where it is stored:
// an assignment, a variable, an initializer list element, a call argument.
static QualType slotType(ASTContext &C, const Expr *E) {
  DynTypedNode Node = DynTypedNode::create(*E);
  for (;;) {
    DynTypedNodeList Parents = C.getParents(Node);
    if (Parents.empty())
      return {};
    const DynTypedNode &P = Parents[0];
    const Stmt *Child = Node.get<Stmt>();
    if (P.get<ImplicitCastExpr>() || P.get<ParenExpr>()) {
      Node = P;
      continue;
    }
    if (const auto *UO = P.get<UnaryOperator>()) {
      if (UO->getOpcode() != UO_AddrOf)
        return {};
      Node = P;
      continue;
    }
    if (const auto *BO = P.get<BinaryOperator>())
      return BO->getOpcode() == BO_Assign && BO->getRHS() == Child
                 ? BO->getLHS()->getType()
                 : QualType();
    if (const auto *VD = P.get<VarDecl>())
      return VD->getType();
    // `.field = f`: the designator names the field.
    if (const auto *DIE = P.get<DesignatedInitExpr>()) {
      if (!DIE->size())
        return {};
      const DesignatedInitExpr::Designator *D =
          DIE->getDesignator(DIE->size() - 1);
      return D->isFieldDesignator() && D->getFieldDecl()
                 ? D->getFieldDecl()->getType()
                 : QualType();
    }
    if (const auto *CE = P.get<CallExpr>()) {
      const FunctionDecl *Callee = CE->getDirectCallee();
      for (unsigned I = 0; Callee && I < CE->getNumArgs(); ++I)
        if (CE->getArg(I) == Child && I < Callee->getNumParams())
          return Callee->getParamDecl(I)->getType();
      return {};
    }
    if (const auto *ILE = P.get<InitListExpr>()) {
      // The semantic form wraps the element in implicit casts.
      unsigned Index = 0;
      const auto *ChildExpr = dyn_cast_or_null<Expr>(Child);
      while (Index < ILE->getNumInits() && ChildExpr &&
             ILE->getInit(Index)->IgnoreParenImpCasts() !=
                 ChildExpr->IgnoreParenImpCasts())
        ++Index;
      if (Index == ILE->getNumInits())
        return {};
      QualType T = ILE->getType();
      if (const auto *AT = C.getAsArrayType(T))
        return AT->getElementType();
      if (const RecordDecl *RD = T->getAsRecordDecl()) {
        if (RD->isUnion())
          return ILE->getInitializedFieldInUnion()
                     ? ILE->getInitializedFieldInUnion()->getType()
                     : QualType();
        for (const FieldDecl *F : RD->fields())
          if (Index-- == 0)
            return F->getType();
      }
      return {};
    }
    return {};
  }
}

// The typedef naming a function-pointer slot type, or null.
static const TypedefNameDecl *slotTypedef(QualType T) {
  for (QualType Cur = T; !Cur.isNull();) {
    if (const auto *TT = Cur->getAs<TypedefType>()) {
      const TypedefNameDecl *TD = TT->getDecl();
      QualType U = TD->getUnderlyingType();
      if (U->isFunctionPointerType() || U->isFunctionProtoType())
        return TD;
      Cur = U;
      continue;
    }
    // `fn *slot` with fn a function typedef.
    Cur = Cur->isPointerType() ? Cur->getPointeeType() : QualType();
  }
  return nullptr;
}

bool Context::fnPtrSlots(const FunctionDecl *FD,
                         llvm::SmallVectorImpl<const TypedefNameDecl *> &Out) {
  if (!CalleesIndexed)
    indexCallees();
  const FunctionDecl *Canon = FD->getCanonicalDecl();
  bool Any = false;
  auto Take = [&](const Expr *E) {
    const TypedefNameDecl *TD = slotTypedef(slotType(C, E));
    if (!TD || !editable(TD->getLocation()))
      return false;
    if (!llvm::is_contained(Out, TD))
      Out.push_back(TD);
    return Any = true;
  };
  for (DeclRefExpr *DRE : Visitor.Refs)
    if (auto *Ref = dyn_cast<FunctionDecl>(DRE->getDecl()))
      if (Ref->getCanonicalDecl() == Canon && !CalleeRefs.contains(DRE) &&
          !Take(DRE))
        return false;
  for (MemberExpr *ME : Visitor.Members)
    if (auto *Ref = dyn_cast<FunctionDecl>(ME->getMemberDecl()))
      if (Ref->getCanonicalDecl() == Canon && !CalleeRefs.contains(ME))
        return false;
  return Any;
}

SourceLocation Context::slotSpot(const TypedefNameDecl *TD, unsigned Index) {
  TypeLoc TL = TD->getTypeSourceInfo()->getTypeLoc();
  if (auto PTL = TL.getAs<PointerTypeLoc>())
    TL = PTL.getPointeeLoc();
  auto FPTL = TL.IgnoreParens().getAs<FunctionProtoTypeLoc>();
  if (!FPTL || Index >= FPTL.getNumParams() || !FPTL.getParam(Index))
    return {};
  std::optional<PointerTypeLoc> Star = starOf(FPTL.getParam(Index));
  if (!Star || Star->getPointeeLoc().getType()->isPointerType() ||
      Star->getPointeeLoc().getType().isConstQualified())
    return {};
  SourceLocation Begin = Star->getPointeeLoc().getBeginLoc();
  return Begin.isMacroID() ? SourceLocation() : Begin;
}

void Context::exportSlotMembers(llvm::json::OStream &J) {
  if (!CalleesIndexed)
    indexCallees();
  J.attributeArray("slotMembers", [&] {
    llvm::DenseSet<std::pair<const FunctionDecl *, const TypedefNameDecl *>>
        Seen;
    for (DeclRefExpr *DRE : Visitor.Refs) {
      auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl());
      if (!FD || CalleeRefs.contains(DRE))
        continue;
      const TypedefNameDecl *TD = slotTypedef(slotType(C, DRE));
      if (!TD || !editable(TD->getLocation()) ||
          !Seen.insert({FD->getCanonicalDecl(), TD}).second)
        continue;
      for (unsigned I = 0; I < FD->getNumParams(); ++I) {
        SourceLocation Spot = slotSpot(TD, I);
        if (Spot.isInvalid())
          continue;
        auto [FID, Offset] = SM.getDecomposedLoc(Spot);
        OptionalFileEntryRef FE = SM.getFileEntryRefForID(FID);
        if (!FE)
          continue;
        StringRef Path = FE->getFileEntry().tryGetRealPathName();
        J.object([&] {
          J.attribute("file", Path.empty() ? FE->getName() : Path);
          J.attribute("offset", Offset);
          J.attribute("usr", usr(FD));
          J.attribute("index", I);
        });
      }
    }
  });
}

llvm::SmallVector<const FunctionDecl *>
Context::functionsInSlot(const TypedefNameDecl *TD) {
  llvm::SmallVector<const FunctionDecl *> Out;
  for (DeclRefExpr *DRE : Visitor.Refs)
    if (auto *Ref = dyn_cast<FunctionDecl>(DRE->getDecl()))
      if (!CalleeRefs.contains(DRE) && slotTypedef(slotType(C, DRE)) == TD &&
          !llvm::is_contained(Out, Ref->getCanonicalDecl()))
        Out.push_back(Ref->getCanonicalDecl());
  return Out;
}

StringRef Context::whyIneligible(FunctionDecl *FD, unsigned Allowed) {
  if (!CalleesIndexed)
    indexCallees();
  if (isa<CXXConstructorDecl, CXXDestructorDecl, CXXConversionDecl>(FD) ||
      FD->isOverloadedOperator() || FD->isMain() || FD->isVariadic() ||
      !FD->getIdentifier())
    return "special function";
  std::optional<llvm::SmallVector<FunctionDecl *>> Insts;
  if ((Allowed & AllowTemplates) && FD->isTemplated())
    Insts = instantiationsOf(FD);
  bool Template = Insts.has_value();
  if (!Template && (FD->isTemplated() || FD->isTemplateInstantiation() ||
                    FD->getTemplatedKind() != FunctionDecl::TK_NonTemplate))
    return "template";
  if (isa_and_nonnull<CoroutineBodyStmt>(FD->getBody()))
    return "coroutine";
  if (auto *MD = dyn_cast<CXXMethodDecl>(FD))
    if (MD->getParent()->isLocalClass() ||
        (!(Allowed & AllowVirtual) &&
         (MD->isVirtual() || MD->size_overridden_methods())))
      return "virtual or local class method";
  if (const FunctionDecl *Def = FD->getDefinition();
      Def && spellsSignature(Def->getBody()))
    return "signature spelled in __PRETTY_FUNCTION__";
  if (FD->isExternallyVisible() && !Options.AllowPublic && !projectMode())
    return "externally visible";
  for (const FunctionDecl *R : FD->redecls()) {
    SourceLocation Loc = R->getLocation();
    if (projectMode() && (Allowed & AllowMacro) && Loc.isMacroID() &&
        editable(SM.getExpansionLoc(Loc)))
      continue;
    if (!editable(Loc))
      return !projectMode()    ? "declared outside the main file"
             : Loc.isMacroID() ? "declared by a macro"
                               : "declared in a system header";
  }
  if (projectMode() && usr(FD).empty())
    return "no USR";
  if (Visitor.UnresolvedFound.contains(FD->getCanonicalDecl()) ||
      (isa<CXXMethodDecl>(FD) ? Visitor.UnresolvedMethods
                              : Visitor.UnresolvedFree)
          .contains(FD->getDeclName()))
    return "name used in a dependent call";
  const FunctionDecl *Canon = FD->getCanonicalDecl();
  // A rewrite that needs no edit at call sites only minds an instantiation
  // taking the address; any other needs every call edited, and calls inside
  // an instantiation cannot be.
  const llvm::DenseSet<const FunctionDecl *> &InInstantiation =
      (Allowed & AllowTemplates) ? Visitor.AddressInInstantiation
                                 : Visitor.UsedInInstantiation;
  if (InInstantiation.contains(Canon))
    return "used from a template instantiation";
  if (Template)
    for (FunctionDecl *Spec : *Insts)
      if (InInstantiation.contains(Spec->getCanonicalDecl()))
        return "used from a template instantiation";
  for (const NamedDecl *ND :
       FD->getDeclContext()->getRedeclContext()->lookup(FD->getDeclName())) {
    auto *Other = dyn_cast<FunctionDecl>(ND);
    if (auto *FTD = dyn_cast<FunctionTemplateDecl>(ND))
      Other = FTD->getTemplatedDecl();
    if (!Other || Other->getCanonicalDecl() != Canon)
      return "overloaded";
  }
  // A reference to a specialization takes the address of the template's
  // signature.
  auto Same = [&](const FunctionDecl *Ref) {
    if (const FunctionTemplateDecl *Primary = Ref->getPrimaryTemplate())
      Ref = Primary->getTemplatedDecl();
    return Ref->getCanonicalDecl() == Canon;
  };
  llvm::SmallVector<const TypedefNameDecl *> Slots;
  bool InSlots = (Allowed & AllowFnPtrSlots) && fnPtrSlots(FD, Slots);
  if (!InSlots) {
    for (DeclRefExpr *DRE : Visitor.Refs)
      if (auto *Ref = dyn_cast<FunctionDecl>(DRE->getDecl()))
        if (Same(Ref) && !CalleeRefs.contains(DRE))
          return "address taken";
    for (MemberExpr *ME : Visitor.Members)
      if (auto *Ref = dyn_cast<FunctionDecl>(ME->getMemberDecl()))
        if (Same(Ref) && !CalleeRefs.contains(ME))
          return "address taken";
  }
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
  if (Cand.Param)
    ByParam[{Cand.Param, Cand.Rewrite}] = Candidates.size();
  else
    ByMethod[{Cand.FD->getCanonicalDecl(), Cand.Rewrite}] = Candidates.size();
  Candidates.push_back(std::move(Cand));
}

// In project mode a function this translation unit only declares is proven
// (or not) by the one that defines it, so a need on it is left to project.ts,
// which settles every need once all translation units are in.
static bool definedElsewhere(const FunctionDecl *FD) {
  const FunctionDecl *Def = FD->getDefinition();
  return !Def || !Def->hasBody();
}

bool Context::live(const ParmVarDecl *P, unsigned Rewrite) const {
  auto It = ByParam.find({P, Rewrite});
  if (It == ByParam.end())
    return projectMode() &&
           definedElsewhere(cast<FunctionDecl>(P->getDeclContext()));
  return Candidates[It->second].Live;
}

bool Context::live(const FunctionDecl *FD, unsigned Rewrite) const {
  auto It = ByMethod.find({FD->getCanonicalDecl(), Rewrite});
  if (It == ByMethod.end())
    return projectMode() && definedElsewhere(FD);
  return Candidates[It->second].Live;
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
    {"span", "refinement", false, createSpan},
    {"lock-guard", "refinement", false, createLockGuard},
    {"unique-ptr", "refinement", false, createUniquePtr},
    {"dead-branch", "identical", false, createDeadBranch},
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
        if (!llvm::all_of(Cand.Needs,
                          [&](const ParmVarDecl *P) {
                            return Ctx.live(P, Cand.Rewrite);
                          }) ||
            !llvm::all_of(Cand.NeedsMethods, [&](const FunctionDecl *FD) {
              return Ctx.live(FD, Cand.Rewrite);
            }))
          Why = R.whyNeedBlocked();
        else
          Why = R.finalize(Cand);
        if (Why.empty())
          Why = whyConditional(Cand);
        if (Why.empty())
          Why = whyLineDirective(Cand);
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

  StringRef whyLineDirective(const Candidate &Cand) {
    for (const auto *Edits : {&Cand.Edits, &Cand.LateEdits})
      for (const FixItHint &Hint : *Edits)
        for (SourceLocation Loc :
             {Hint.RemoveRange.getBegin(), Hint.RemoveRange.getEnd()}) {
          if (Loc.isInvalid())
            continue;
          SourceLocation Spelling = Ctx.SM.getSpellingLoc(Loc);
          PresumedLoc P = Ctx.SM.getPresumedLoc(Spelling);
          if (P.isInvalid() || P.getFileID() != Ctx.SM.getFileID(Spelling) ||
              P.getLine() != Ctx.SM.getSpellingLineNumber(Spelling) ||
              P.getFilename() != Ctx.SM.getFilename(Spelling))
            return "an edit is under a #line directive";
        }
    return {};
  }

  void report(DiagnosticsEngine &Diags) {
    llvm::SmallVector<unsigned> IDs;
    for (auto &R : Rewrites)
      IDs.push_back(R->diagID(Diags));
    Rewriter RW(Ctx.SM, Ctx.C.getLangOpts());
    std::set<std::tuple<SourceLocation, SourceLocation, std::string>> Applied;
    for (const Candidate &Cand : Ctx.Candidates) {
      if (!Cand.Live)
        continue;
      auto Diag = Diags.Report(Cand.Loc.isValid() ? Cand.Loc
                               : Cand.Param       ? Cand.Param->getLocation()
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
          // Candidates that share an edit (a callback typedef, a macro) make
          // it once.
          if (!Applied
                   .insert({Hint.RemoveRange.getBegin(),
                            Hint.RemoveRange.getEnd(), Hint.CodeToInsert})
                   .second)
            continue;
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
            if (Ctx.exported(Cand.FD))
              J.attribute("exported", true);
            J.attribute("changesType", Rewrites[Cand.Rewrite]->changesType());
            J.attribute("refines", Rewrites[Cand.Rewrite]->refines());
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
              for (const FunctionDecl *FD : Cand.NeedsMethods)
                J.object([&] {
                  J.attribute("usr", Ctx.usr(FD));
                  J.attribute("index", 0);
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
      Ctx.exportSlotMembers(J);
      // A header a C translation unit includes can take only C syntax; the
      // C++ translation units that propose edits to it cannot tell.
      if (!Ctx.C.getLangOpts().CPlusPlus)
        J.attributeArray("cFiles", [&] {
          for (const auto &[FE, Info] : llvm::make_range(
                   Ctx.SM.fileinfo_begin(), Ctx.SM.fileinfo_end())) {
            StringRef Path = FE.getFileEntry().tryGetRealPathName();
            J.value(Path.empty() ? FE.getName() : Path);
          }
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
  // `#define NAME ...` with an export attribute in its body, in a branch this
  // build skips: NAME exports in some other build (a shared one).
  enum { Line, Hash, Define, Body } State = Line;
  StringRef Defined;
  while (!L.LexFromRawLexer(T) && SM.getFileOffset(T.getLocation()) <= End) {
    if (T.isAtStartOfLine())
      State = T.is(tok::hash) ? Hash : Line;
    else if (State == Hash)
      State = T.is(tok::raw_identifier) && T.getRawIdentifier() == "define"
                  ? Define
                  : Line;
    else if (State == Define) {
      Defined = T.is(tok::raw_identifier) ? T.getRawIdentifier() : "";
      State = Defined.empty() ? Line : Body;
    } else if (State == Body && T.is(tok::raw_identifier) &&
               isExportWord(T.getRawIdentifier()))
      ExportMacros.insert(Defined);
    if (T.is(tok::raw_identifier))
      Skipped.insert(T.getRawIdentifier());
  }
}

bool ConditionalRecord::isExportWord(StringRef Word) {
  return Word == "visibility" || Word == "dllexport" || Word == "dllimport";
}

void ConditionalRecord::MacroDefined(const Token &Name,
                                     const MacroDirective *MD) {
  const MacroInfo *MI = MD ? MD->getMacroInfo() : nullptr;
  if (!MI || PP.getSourceManager().isInSystemHeader(MI->getDefinitionLoc()))
    return;
  for (const Token &T : MI->tokens())
    if (const IdentifierInfo *II = T.getIdentifierInfo();
        II && isExportWord(II->getName()))
      ExportMacros.insert(Name.getIdentifierInfo()->getName());
}

void ConditionalRecord::MacroExpands(const Token &Name,
                                     const MacroDefinition &MD,
                                     SourceRange Range, const MacroArgs *) {
  const MacroInfo *MI = MD.getMacroInfo();
  if (!MI || !MI->isObjectLike() || MI->getNumTokens() != 1)
    return;
  const Token &T = MI->getReplacementToken(0);
  if (!T.isOneOf(tok::kw__Nonnull, tok::kw__Nullable))
    return;
  SourceManager &SM = PP.getSourceManager();
  if (SM.isInSystemHeader(MI->getDefinitionLoc()) ||
      SM.isInSystemHeader(Range.getBegin()))
    return;
  ++NullabilityUses[T.is(tok::kw__Nonnull)]
                   [Name.getIdentifierInfo()->getName()];
  NullabilityFiles.insert(SM.getFileID(SM.getExpansionLoc(Range.getBegin())));
}

StringRef ConditionalRecord::nullabilityMacro(bool NonNull) const {
  StringRef Best;
  unsigned Most = 0;
  for (const auto &E : NullabilityUses[NonNull])
    if (E.getValue() > Most || (E.getValue() == Most && E.getKey() < Best)) {
      Best = E.getKey();
      Most = E.getValue();
    }
  return Best;
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
