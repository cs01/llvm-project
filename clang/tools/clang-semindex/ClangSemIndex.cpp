//===- ClangSemIndex.cpp - List the named entities a header declares ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Parses a translation unit that includes a header and prints, as JSON, every
// named entity the header declares together with a hash of its tokens. A build
// tool diffs two such indexes (before and after an edit) to learn which names
// the edit touched; a translation unit that never materialized any of those
// names (see -fsemantic-deps) cannot have its output changed by the edit.
//
// Tokens outside every named entity and macro (pragmas, using-directives,
// #includes, static_asserts, ...) are hashed together as "rest", and the build
// tool treats a change to them, or to any macro, as affecting every dependent.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/BLAKE3.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <optional>
#include <set>

using namespace clang;
using namespace llvm;

static cl::OptionCategory Category("clang-semindex options");
static cl::opt<std::string> BuildDir("p", cl::desc("Build directory"),
                                     cl::Required, cl::cat(Category));
static cl::opt<std::string> Header("header", cl::desc("Header to index"),
                                   cl::Required, cl::cat(Category));
static cl::opt<std::string>
    OldContent("contents",
               cl::desc("File whose bytes stand in for the header's"),
               cl::cat(Category));
static SmallString<256> HeaderRealPath;
static cl::opt<std::string>
    Source(cl::Positional, cl::desc("<source whose compile command to use>"),
           cl::Required, cl::cat(Category));
static cl::opt<bool> InSource(
    "in-source",
    cl::desc("Parse the header as the source includes it, rather than alone "
             "(for a header that does not compile by itself)"),
    cl::cat(Category));

namespace {

struct Entity {
  std::string Key;
  std::vector<std::string> Names;
  // A changed entity with no identifier name (an operator at namespace scope,
  // say) can be found without naming it, so its changes affect everyone.
  bool Nameless = false;
  // The body of a function: a change to it only matters to compiles that
  // loaded the body, which -fsemantic-deps records as "<name>()".
  bool Body = false;
  std::string Hash;
};

/// A graph node: an entity (or a function body, named "<entity>()") and the
/// entities it refers to. "?name" refers to whatever a dependent name in a
/// template resolves to when instantiated.
struct Node {
  std::vector<std::string> Names;
  std::string BodyName;
  std::set<std::string> Refs;
};

struct Index {
  FileID FID;
  std::map<std::string, Node> Graph;
  std::vector<Entity> Entities;
  std::map<std::string, std::string> Macros;
  // Byte ranges [Begin, End) covered by an entity or macro directive.
  std::vector<std::pair<unsigned, unsigned>> Claimed;
  // Offsets whose line (and column) is observable in the output.
  std::set<unsigned> LineSensitive;
  bool DebugInfo = false;
  // __COUNTER__ values depend on every earlier use, so any edit can move them.
  bool UsesCounter = false;
  // Macro expansion sites in every file: (offset, macro name).
  std::map<FileID, std::vector<std::pair<unsigned, std::string>>> Expansions;
  std::vector<SourceRange> ConditionRanges;
  std::set<std::string> ConditionMacros;
  // What every includer emits, named, so a reader can see why.
  std::vector<std::string> EmittedDecls;
  std::string Rest;
};

class Collector : public PPCallbacks {
  Index &Idx;
  SourceManager &SM;
  Preprocessor &PP;

  bool inHeader(SourceLocation L) const {
    return L.isValid() && SM.getFileID(SM.getExpansionLoc(L)) == Idx.FID;
  }

  void claimDirectiveLine(SourceLocation NameLoc, SourceLocation EndLoc) {
    if (!inHeader(NameLoc))
      return;
    unsigned B = SM.getFileOffset(SM.getExpansionLoc(NameLoc));
    StringRef Buf = SM.getBufferData(Idx.FID);
    while (B > 0 && Buf[B - 1] != '\n')
      --B;
    unsigned E =
        EndLoc.isValid()
            ? SM.getFileOffset(SM.getExpansionLoc(
                  Lexer::getLocForEndOfToken(EndLoc, 0, SM, PP.getLangOpts())))
            : B;
    while (E < Buf.size() && Buf[E] != '\n')
      ++E;
    Idx.Claimed.push_back({B, E});
  }

public:
  // Macros are told apart by definition, not name: helper macros like STMT
  // are defined, used and #undef'd in many headers.
  llvm::DenseMap<const MacroInfo *, std::string> MacroIds;
  std::map<std::pair<FileID, std::string>, unsigned> DefinitionsSeen;

  /// "NAME@<file>#<n>" for the n-th definition of NAME in that file.
  std::string macroId(StringRef Name, const MacroInfo *MI) {
    auto [It, New] = MacroIds.try_emplace(MI);
    if (!New)
      return It->second;
    std::string File = "<builtin>";
    SourceLocation L = MI->getDefinitionLoc();
    unsigned N = 0;
    if (L.isValid()) {
      FileID F = SM.getFileID(SM.getExpansionLoc(L));
      if (OptionalFileEntryRef FE = SM.getFileEntryRefForID(F))
        File = FE->getName().str();
      N = DefinitionsSeen[{F, Name.str()}]++;
    }
    It->second = (Name + "@" + File + "#" + Twine(N)).str();
    return It->second;
  }

  Collector(Index &Idx, SourceManager &SM, Preprocessor &PP)
      : Idx(Idx), SM(SM), PP(PP) {}

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind, FileID) override {
    if (Reason != EnterFile || Idx.FID.isValid())
      return;
    OptionalFileEntryRef FE = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!FE)
      return;
    SmallString<256> Real;
    if (sys::fs::real_path(FE->getName(), Real))
      Real = FE->getName();
    if (Real == HeaderRealPath)
      Idx.FID = SM.getFileID(Loc);
  }

  void MacroExpands(const Token &Name, const MacroDefinition &MD,
                    SourceRange Range, const MacroArgs *) override {
    // __LINE__ bakes the line of its outermost expansion site into the code
    // (assert does this), so a decl containing one changes when it moves.
    if (Name.getIdentifierInfo()->getName() == "__LINE__" &&
        inHeader(Range.getBegin()))
      Idx.LineSensitive.insert(
          SM.getFileOffset(SM.getExpansionLoc(Range.getBegin())));
    if (Name.getIdentifierInfo()->getName() == "__COUNTER__" &&
        inHeader(Range.getBegin()))
      Idx.UsesCounter = true;
    // Where each macro definition was expanded, so the entities containing
    // the expansion can be found to depend on it. Expansions nested in another
    // macro's are recorded at the outer one's site.
    SourceLocation L = SM.getExpansionLoc(Range.getBegin());
    if (L.isValid() && L.isFileID())
      if (const MacroInfo *MI = MD.getMacroInfo()) {
        auto [FID, Offset] = SM.getDecomposedLoc(L);
        Idx.Expansions[FID].push_back(
            {Offset, macroId(Name.getIdentifierInfo()->getName(), MI)});
      }
  }

  // A macro tested by a conditional decides which declarations exist, in
  // headers that are not re-indexed when the macro changes.
  void If(SourceLocation, SourceRange Cond, ConditionValueKind) override {
    Idx.ConditionRanges.push_back(Cond);
  }
  void Elif(SourceLocation, SourceRange Cond, ConditionValueKind,
            SourceLocation) override {
    Idx.ConditionRanges.push_back(Cond);
  }
  void Ifdef(SourceLocation, const Token &Name,
             const MacroDefinition &) override {
    Idx.ConditionMacros.insert(Name.getIdentifierInfo()->getName().str());
  }
  void Ifndef(SourceLocation, const Token &Name,
              const MacroDefinition &) override {
    Idx.ConditionMacros.insert(Name.getIdentifierInfo()->getName().str());
  }
  void Defined(const Token &Name, const MacroDefinition &,
               SourceRange) override {
    Idx.ConditionMacros.insert(Name.getIdentifierInfo()->getName().str());
  }

  void MacroDefined(const Token &Name, const MacroDirective *MD) override {
    const MacroInfo *MI = MD->getMacroInfo();
    std::string Id = macroId(Name.getIdentifierInfo()->getName(), MI);
    if (!inHeader(MI->getDefinitionLoc()))
      return;
    BLAKE3 H;
    H.update(MI->isFunctionLike() ? "(" : "-");
    for (const IdentifierInfo *P : MI->params()) {
      H.update(P->getName());
      H.update(",");
    }
    H.update(MI->isVariadic() ? "..." : "");
    for (const Token &T : MI->tokens()) {
      H.update(PP.getSpelling(T));
      H.update(T.hasLeadingSpace() ? " " : "\x1f");
    }
    Idx.Macros[Id] = toHex(H.final(), /*LowerCase=*/true);
    claimDirectiveLine(MI->getDefinitionLoc(), MI->getDefinitionEndLoc());
  }

  void MacroUndefined(const Token &Name, const MacroDefinition &,
                      const MacroDirective *) override {
    if (!inHeader(Name.getLocation()))
      return;
    // Where it is undefined is part of the rest of the file; that it is, is
    // what changes which definition later uses see.
    Idx.Macros["#undef " + Name.getIdentifierInfo()->getName().str()] = "";
    claimDirectiveLine(Name.getLocation(), Name.getLocation());
  }
};

/// Finds expressions whose value is the source location they are used at
/// (__builtin_LINE, std::source_location::current() as a default argument),
/// so a declaration containing one changes when it moves.
class LocationUseFinder : public RecursiveASTVisitor<LocationUseFinder> {
  Index &Idx;
  SourceManager &SM;

  void note(SourceLocation L) {
    L = SM.getExpansionLoc(L);
    if (SM.getFileID(L) == Idx.FID)
      Idx.LineSensitive.insert(SM.getFileOffset(L));
  }

  static bool containsSourceLocExpr(const Stmt *S) {
    if (!S)
      return false;
    if (isa<SourceLocExpr>(S))
      return true;
    for (const Stmt *C : S->children())
      if (containsSourceLocExpr(C))
        return true;
    return false;
  }

public:
  LocationUseFinder(Index &Idx, SourceManager &SM) : Idx(Idx), SM(SM) {}

  bool VisitSourceLocExpr(SourceLocExpr *E) {
    note(E->getLocation());
    return true;
  }
  bool VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E) {
    if (containsSourceLocExpr(E->getParam()->getDefaultArg()))
      note(E->getUsedLocation());
    return true;
  }
  bool VisitCXXDefaultInitExpr(CXXDefaultInitExpr *E) {
    if (containsSourceLocExpr(E->getField()->getInClassInitializer()))
      note(E->getUsedLocation());
    return true;
  }
};

/// The name a compile records for using D (see -fsemantic-deps): a member of
/// a class as "Class::member", since it records every name it looks up in a
/// class that way; anything else by its bare name, as lexed.
static std::string useName(const NamedDecl *D) {
  if (!D->getDeclName().isIdentifier())
    return "";
  const DeclContext *DC = D->getDeclContext();
  // Enumerators of an unscoped enum are found in the enum's scope; those of
  // a scoped one are only ever named through the enum.
  if (const auto *ED = dyn_cast<EnumDecl>(DC)) {
    if (ED->isScoped())
      return D->getName().str();
    DC = DC->getParent();
  }
  if (const auto *RD = dyn_cast<RecordDecl>(DC);
      RD && RD->getDeclName().isIdentifier())
    return (RD->getName() + "::" + D->getName()).str();
  return D->getName().str();
}

/// The key prefix an entity of DC gets ("clang::FunctionDecl::"), matching
/// the prefixes IndexConsumer builds, or nullopt for a context that holds no
/// entities (a function, say).
static std::optional<std::string> scopeOf(const DeclContext *DC) {
  SmallVector<const DeclContext *, 8> Chain;
  for (; DC && !DC->isTranslationUnit(); DC = DC->getParent())
    Chain.push_back(DC);
  std::string S;
  for (const DeclContext *C : llvm::reverse(Chain)) {
    if (const auto *NS = dyn_cast<NamespaceDecl>(C))
      S += (NS->isAnonymousNamespace() ? "(anon)" : NS->getName().str()) + "::";
    else if (const auto *RD = dyn_cast<RecordDecl>(C))
      S += (RD->getDeclName().isIdentifier() ? RD->getName().str() : "") + "::";
    else if (!isa<LinkageSpecDecl, ExportDecl>(C))
      return std::nullopt;
  }
  return S;
}

/// Collects what part of a declaration refers to, as graph node names.
class RefCollector : public RecursiveASTVisitor<RefCollector> {
  std::set<std::string> &Refs;

  void ref(const Decl *D) {
    if (!D)
      return;
    // Fields, enumerators and the non-name-only members are part of the
    // entity that declares them.
    if (isa<FieldDecl, EnumConstantDecl, IndirectFieldDecl>(D))
      D = cast<Decl>(D->getDeclContext());
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      if (const FunctionDecl *P = FD->getTemplateInstantiationPattern())
        D = P;
    if (const auto *RD = dyn_cast<CXXRecordDecl>(D))
      if (const CXXRecordDecl *P = RD->getTemplateInstantiationPattern())
        D = P;
    const auto *ND = dyn_cast<NamedDecl>(D);
    if (!ND ||
        isa<ParmVarDecl, TemplateTypeParmDecl, NonTypeTemplateParmDecl>(ND))
      return;
    if (std::optional<std::string> S = scopeOf(ND->getDeclContext()))
      Refs.insert((Weak ? "&" : "") + *S + ND->getDeclName().getAsString());
  }

  // A type named only through a pointer or reference does not depend on its
  // definition, only on its existence: such uses are recorded as "&<name>".
  bool Weak = false;
  template <typename TL> bool weak(TL T) {
    llvm::SaveAndRestore S(Weak, true);
    return TraverseTypeLoc(T.getPointeeLoc());
  }

public:
  explicit RefCollector(std::set<std::string> &Refs) : Refs(Refs) {}
  bool shouldVisitTemplateInstantiations() const { return false; }

  bool TraversePointerTypeLoc(PointerTypeLoc TL, bool) { return weak(TL); }
  bool TraverseLValueReferenceTypeLoc(LValueReferenceTypeLoc TL, bool) {
    return weak(TL);
  }
  bool TraverseRValueReferenceTypeLoc(RValueReferenceTypeLoc TL, bool) {
    return weak(TL);
  }

  bool VisitDeclRefExpr(DeclRefExpr *E) {
    ref(E->getDecl());
    return true;
  }
  bool VisitMemberExpr(MemberExpr *E) {
    ref(E->getMemberDecl());
    return true;
  }
  bool VisitCXXConstructExpr(CXXConstructExpr *E) {
    ref(E->getConstructor());
    return true;
  }
  bool VisitTagTypeLoc(TagTypeLoc TL) {
    ref(TL.getDecl());
    return true;
  }
  bool VisitTypedefTypeLoc(TypedefTypeLoc TL) {
    ref(TL.getDecl());
    return true;
  }
  bool VisitTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc TL) {
    ref(TL.getTypePtr()->getTemplateName().getAsTemplateDecl());
    return true;
  }
  bool VisitUsingType(UsingType *T) {
    ref(T->getDecl());
    return true;
  }
  bool VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E) {
    Refs.insert("?" + E->getName().getAsString());
    return true;
  }
  bool VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
    Refs.insert("?" + E->getMemberName().getAsString());
    return true;
  }
  bool VisitCXXDependentScopeMemberExpr(CXXDependentScopeMemberExpr *E) {
    Refs.insert("?" + E->getMember().getAsString());
    return true;
  }
  bool VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E) {
    Refs.insert("?" + E->getDeclName().getAsString());
    return true;
  }
  bool VisitDependentNameTypeLoc(DependentNameTypeLoc TL) {
    Refs.insert("?" + TL.getTypePtr()->getIdentifier()->getName().str());
    return true;
  }
};

/// Builds the reference graph over every entity the TU's headers declare, so
/// a change can be followed to everything that uses it: `constexpr int B =
/// A + 1` changes when A does, though B's own tokens do not.
class GraphBuilder {
  std::map<std::string, Node> &Graph;
  const SourceManager &SM;

public:
  /// The source each node covers, to find the macros expanded in it.
  std::map<std::string, std::vector<CharSourceRange>> Ranges;

private:
  bool inHeaderFile(const Decl *D) const {
    SourceLocation L = SM.getExpansionLoc(D->getLocation());
    return L.isValid() && SM.getFileID(L) != SM.getMainFileID();
  }

  Node &node(const NamedDecl *ND, const std::string &Prefix) {
    std::string Q = Prefix + ND->getDeclName().getAsString();
    Node &N = Graph[Q];
    Ranges[Q].push_back(CharSourceRange::getTokenRange(ND->getSourceRange()));
    if (ND->getDeclName().isIdentifier())
      N.Names.push_back(useName(ND));
    else if (const auto *M = dyn_cast<CXXMethodDecl>(ND))
      N.Names.push_back(useName(M->getParent()));
    return N;
  }

  void function(const FunctionDecl *FD, const std::string &Prefix,
                std::set<std::string> &DeclRefs) {
    RefCollector C(DeclRefs);
    if (TypeSourceInfo *TSI = FD->getTypeSourceInfo())
      C.TraverseTypeLoc(TSI->getTypeLoc());
    for (const ParmVarDecl *P : FD->parameters())
      if (P->hasDefaultArg() && !P->hasUninstantiatedDefaultArg() &&
          !P->hasUnparsedDefaultArg())
        C.TraverseStmt(const_cast<Expr *>(P->getDefaultArg()));
    if (!FD->doesThisDeclarationHaveABody())
      return;
    std::string Q = Prefix + FD->getDeclName().getAsString() + "()";
    Node &B = Graph[Q];
    Ranges[Q].push_back(CharSourceRange::getTokenRange(FD->getSourceRange()));
    B.BodyName =
        (isa<RecordDecl>(FD->getDeclContext())
             ? cast<RecordDecl>(FD->getDeclContext())->getNameAsString() + "::"
             : std::string()) +
        FD->getDeclName().getAsString();
    RefCollector BC(B.Refs);
    if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(FD))
      for (const CXXCtorInitializer *Init : Ctor->inits())
        if (Init->isWritten())
          BC.TraverseConstructorInitializer(
              const_cast<CXXCtorInitializer *>(Init));
    BC.TraverseStmt(FD->getBody());
    // A body that determines the function's type is part of its declaration.
    if (FD->getDeclaredReturnType()->getContainedDeducedType())
      DeclRefs.insert(B.Refs.begin(), B.Refs.end());
  }

public:
  GraphBuilder(std::map<std::string, Node> &Graph, const SourceManager &SM)
      : Graph(Graph), SM(SM) {}

  void visit(const Decl *D, const std::string &Prefix) {
    if (D->isImplicit())
      return;
    if (const auto *NS = dyn_cast<NamespaceDecl>(D)) {
      std::string N =
          NS->isAnonymousNamespace() ? "(anon)" : NS->getName().str();
      for (const Decl *C : NS->decls())
        visit(C, Prefix + N + "::");
      return;
    }
    if (isa<LinkageSpecDecl, ExportDecl>(D)) {
      for (const Decl *C : cast<DeclContext>(D)->decls())
        visit(C, Prefix);
      return;
    }
    if (!inHeaderFile(D))
      return;
    const auto *ND = dyn_cast<NamedDecl>(D);
    if (!ND)
      return;
    const Decl *Inner = D;
    if (const auto *T = dyn_cast<TemplateDecl>(D); T && T->getTemplatedDecl())
      Inner = T->getTemplatedDecl();
    Node &N = node(ND, Prefix);
    RefCollector C(N.Refs);
    if (const auto *T = dyn_cast<TemplateDecl>(D))
      if (TemplateParameterList *TPL = T->getTemplateParameters())
        for (const NamedDecl *P : *TPL)
          C.TraverseDecl(const_cast<NamedDecl *>(P));
    if (const auto *FD = dyn_cast<FunctionDecl>(Inner)) {
      // Out-of-line members belong to their class's scope.
      std::string P = Prefix;
      if (isa<CXXMethodDecl>(FD) &&
          FD->getLexicalDeclContext() != FD->getDeclContext())
        P = scopeOf(FD->getDeclContext()).value_or(Prefix);
      function(FD, P, N.Refs);
      return;
    }
    if (const auto *RD = dyn_cast<CXXRecordDecl>(Inner);
        RD && RD->isThisDeclarationADefinition()) {
      std::string Scope = Prefix + RD->getNameAsString() + "::";
      for (const CXXBaseSpecifier &B : RD->bases())
        C.TraverseTypeLoc(B.getTypeSourceInfo()->getTypeLoc());
      for (const Decl *M : RD->decls()) {
        if (M->isImplicit())
          continue;
        const Decl *MI = M;
        if (const auto *FT = dyn_cast<FunctionTemplateDecl>(M))
          MI = FT->getTemplatedDecl();
        if (const auto *MF = dyn_cast<FunctionDecl>(MI)) {
          // The class depends on its members' declarations; each body is its
          // own node.
          std::set<std::string> Sig;
          function(MF, Scope, Sig);
          N.Refs.insert(Sig.begin(), Sig.end());
          Node &MN = node(cast<NamedDecl>(M), Scope);
          MN.Refs.insert(Sig.begin(), Sig.end());
          continue;
        }
        if (isa<CXXRecordDecl, ClassTemplateDecl, EnumDecl, TypedefNameDecl,
                TypeAliasTemplateDecl, VarDecl, VarTemplateDecl>(M)) {
          visit(M, Scope);
          N.Refs.insert(Scope +
                        cast<NamedDecl>(M)->getDeclName().getAsString());
          continue;
        }
        C.TraverseDecl(const_cast<Decl *>(M));
      }
      return;
    }
    C.TraverseDecl(const_cast<Decl *>(Inner));
  }
};

class IndexConsumer : public ASTConsumer {
  Index &Idx;
  SourceManager *SM = nullptr;
  const LangOptions *LO = nullptr;
  ASTContext *Ctx = nullptr;
  // Declarations every compile that includes the header emits, used or not
  // (a non-inline variable defined in a header, say).
  std::vector<std::pair<unsigned, unsigned>> Emitted;
  std::vector<std::string> EmittedNames;

  bool inHeader(SourceLocation L) const {
    return L.isValid() && SM->getFileID(SM->getExpansionLoc(L)) == Idx.FID;
  }

  std::pair<unsigned, unsigned> offsets(SourceRange R) const {
    SourceLocation B = SM->getExpansionLoc(R.getBegin());
    SourceLocation E = SM->getExpansionLoc(R.getEnd());
    // A declaration's range stops before its ';'. Claim it too, or adding a
    // declaration would look like a change to whatever encloses it.
    SourceLocation AfterSemi = Lexer::findLocationAfterToken(
        E, tok::semi, *SM, *LO, /*SkipTrailingWhitespaceAndNewLine=*/false);
    E = AfterSemi.isValid() ? AfterSemi
                            : Lexer::getLocForEndOfToken(E, 0, *SM, *LO);
    return {SM->getFileOffset(B), SM->getFileOffset(E)};
  }

  void addPosition(BLAKE3 &H, unsigned Offset) const {
    H.update(std::to_string(SM->getLineNumber(Idx.FID, Offset)));
    H.update(":");
    H.update(std::to_string(SM->getColumnNumber(Idx.FID, Offset)));
    H.update("\x1f");
  }

  /// Hashes the tokens in [B, E) outside the Holes, ignoring whitespace and
  /// comments, plus the position of anything whose location the output sees.
  std::string hashRange(unsigned B, unsigned E,
                        ArrayRef<std::pair<unsigned, unsigned>> Holes) const {
    StringRef Buf = SM->getBufferData(Idx.FID);
    Lexer L(SM->getLocForStartOfFile(Idx.FID), *LO, Buf.begin(),
            Buf.begin() + B, Buf.end());
    BLAKE3 H;
    Token T;
    auto InHole = [&](unsigned O) {
      for (auto [HB, HE] : Holes)
        if (O >= HB && O < HE)
          return true;
      return false;
    };
    while (true) {
      L.LexFromRawLexer(T);
      if (T.is(tok::eof))
        break;
      unsigned O = SM->getFileOffset(T.getLocation());
      if (O >= E)
        break;
      if (InHole(O))
        continue;
      StringRef S(Buf.data() + O, T.getLength());
      H.update(S);
      H.update("\x1f");
      if (Idx.DebugInfo)
        addPosition(H, O);
    }
    for (auto I = Idx.LineSensitive.lower_bound(B),
              End = Idx.LineSensitive.lower_bound(E);
         I != End; ++I)
      if (!InHole(*I))
        addPosition(H, *I);
    return toHex(H.final(), /*LowerCase=*/true);
  }

  void add(std::string Key, std::vector<std::string> Names, SourceRange R,
           ArrayRef<std::pair<unsigned, unsigned>> Holes = {},
           bool Body = false) {
    if (R.isInvalid() || !inHeader(R.getBegin()) || !inHeader(R.getEnd()))
      return;
    auto [B, E] = Body ? bodyOffsets(R) : offsets(R);
    Entity Ent;
    Ent.Key = std::move(Key);
    Ent.Nameless = Names.empty();
    Ent.Names = std::move(Names);
    Ent.Body = Body;
    Ent.Hash = hashRange(B, E, Holes);
    Idx.Entities.push_back(std::move(Ent));
    Idx.Claimed.push_back({B, E});
  }

  /// A body ends at its closing brace; unlike a declaration it has no ';'.
  std::pair<unsigned, unsigned> bodyOffsets(SourceRange R) const {
    SourceLocation B = SM->getExpansionLoc(R.getBegin());
    SourceLocation E = Lexer::getLocForEndOfToken(
        SM->getExpansionLoc(R.getEnd()), 0, *SM, *LO);
    return {SM->getFileOffset(B), SM->getFileOffset(E)};
  }

  /// The part of a function definition that only code using the body can
  /// observe: constructor initializers and the body itself. Functions whose
  /// return type is deduced are not split, as their body determines their
  /// type.
  std::optional<SourceRange> splittableBody(const Decl *D) const {
    if (auto *FT = dyn_cast<FunctionTemplateDecl>(D))
      D = FT->getTemplatedDecl();
    const auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD || !FD->doesThisDeclarationHaveABody() || !FD->getBody() ||
        FD->getDeclaredReturnType()->getContainedDeducedType())
      return std::nullopt;
    SourceLocation Begin = FD->getBody()->getBeginLoc();
    if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(FD))
      for (const CXXCtorInitializer *Init : Ctor->inits())
        if (Init->isWritten() && Init->getSourceLocation().isValid()) {
          Begin = Init->getSourceLocation();
          break;
        }
    SourceRange R(Begin, FD->getBody()->getEndLoc());
    if (!inHeader(R.getBegin()) || !inHeader(R.getEnd()))
      return std::nullopt;
    return R;
  }

  /// Adds the body of D as its own entity and returns its range as a hole for
  /// the enclosing entity, if D has a body worth splitting.
  std::optional<std::pair<unsigned, unsigned>> addBody(const Decl *D,
                                                       const std::string &Key) {
    std::optional<SourceRange> R = splittableBody(D);
    if (!R)
      return std::nullopt;
    add(Key + "#body", {bodyName(D)}, *R, {}, /*Body=*/true);
    return bodyOffsets(*R);
  }

  /// Bodies are named with their class (FieldDecl::getFieldIndex), matching
  /// what -fsemantic-deps records for a loaded body.
  static std::string bodyName(const Decl *D) {
    if (auto *FT = dyn_cast<FunctionTemplateDecl>(D))
      D = FT->getTemplatedDecl();
    const auto *ND = cast<NamedDecl>(D);
    std::string Name;
    if (const auto *RD = dyn_cast<RecordDecl>(ND->getDeclContext()))
      Name = RD->getDeclName().getAsString() + "::";
    return Name + ND->getDeclName().getAsString();
  }

  static std::string nameOf(const NamedDecl *D) {
    return D->getDeclName().isIdentifier() ? D->getName().str() : "";
  }

  /// Members whose addition or change can only matter to code that names
  /// them: everything else about a class (fields, bases, virtuals, special
  /// members, conversions, friends) changes the class itself.
  static bool isNameOnlyMember(const Decl *D) {
    if (auto *FT = dyn_cast<FunctionTemplateDecl>(D))
      D = FT->getTemplatedDecl();
    if (auto *M = dyn_cast<CXXMethodDecl>(D))
      return !isa<CXXConstructorDecl, CXXDestructorDecl, CXXConversionDecl>(
                 M) &&
             M->getDeclName().isIdentifier() && !M->isVirtual();
    if (auto *V = dyn_cast<VarDecl>(D))
      return V->isStaticDataMember();
    return isa<TypedefNameDecl, TypeAliasTemplateDecl, UsingDecl, EnumDecl,
               VarTemplateDecl>(D) ||
           (isa<CXXRecordDecl, ClassTemplateDecl>(D) &&
            cast<NamedDecl>(D)->getDeclName().isIdentifier());
  }

  void visitClass(const CXXRecordDecl *RD, SourceRange R,
                  const std::string &Prefix) {
    std::string Name = nameOf(RD);
    std::string Scope = Prefix + Name + "::";
    std::vector<std::pair<unsigned, unsigned>> Holes;
    for (const Decl *M : RD->decls()) {
      if (M->isImplicit() || !inHeader(M->getLocation()))
        continue;
      if (!isNameOnlyMember(M)) {
        // The declaration stays part of the class; its body does not.
        if (const auto *ND = dyn_cast<NamedDecl>(M)) {
          if (auto Hole = addBody(M, Scope + ND->getDeclName().getAsString()))
            Holes.push_back(*Hole);
        }
        continue;
      }
      SourceRange MR = M->getSourceRange();
      if (MR.isInvalid() || !inHeader(MR.getBegin()) || !inHeader(MR.getEnd()))
        continue;
      Holes.push_back(offsets(MR));
      visitDecl(M, Scope);
    }
    // visitDecl claimed the members; the class claims the whole range.
    add(Prefix + Name + "#class", {useName(RD)}, R, Holes);
  }

  void visitDecl(const Decl *D, const std::string &Prefix) {
    if (D->isImplicit())
      return;
    if (auto *NS = dyn_cast<NamespaceDecl>(D)) {
      std::string N =
          NS->isAnonymousNamespace() ? "(anon)" : NS->getName().str();
      // Opening or closing a namespace changes nothing by itself; what it
      // holds is indexed below. Inline namespaces are in every key inside.
      if (inHeader(NS->getBeginLoc()) && inHeader(NS->getRBraceLoc())) {
        SourceLocation Name =
            NS->isAnonymousNamespace() ? NS->getBeginLoc() : NS->getLocation();
        auto [B, E] = offsets(SourceRange(NS->getBeginLoc(), Name));
        if (std::optional<Token> LBrace =
                Lexer::findNextToken(SM->getExpansionLoc(Name), *SM, *LO);
            LBrace && LBrace->is(tok::l_brace))
          E = SM->getFileOffset(LBrace->getEndLoc());
        Idx.Claimed.push_back({B, E});
        Idx.Claimed.push_back(offsets(SourceRange(NS->getRBraceLoc())));
      }
      for (const Decl *C : NS->decls())
        visitDecl(C, Prefix + N + "::");
      return;
    }
    if (isa<LinkageSpecDecl, ExportDecl>(D)) {
      for (const Decl *C : cast<DeclContext>(D)->decls())
        visitDecl(C, Prefix);
      return;
    }
    if (!inHeader(D->getLocation()))
      return;
    SourceRange R = D->getSourceRange();
    if (Ctx->DeclMustBeEmitted(D) && inHeader(R.getBegin()) &&
        inHeader(R.getEnd())) {
      Emitted.push_back(offsets(R));
      if (const auto *ND = dyn_cast<NamedDecl>(D))
        Idx.EmittedDecls.push_back(ND->getQualifiedNameAsString());
    }
    // A leading [[attribute]] is part of the declaration but not of its range.
    for (const Attr *A : D->attrs())
      if (A->getRange().isValid() && inHeader(A->getLocation()) &&
          SM->isBeforeInTranslationUnit(
              SM->getExpansionLoc(A->getRange().getBegin()),
              SM->getExpansionLoc(R.getBegin())))
        R.setBegin(A->getRange().getBegin());

    // An explicit instantiation definition makes every includer emit it.
    auto NoteInstantiations = [&](auto *T) {
      for (const auto *S : T->specializations())
        if (S->getTemplateSpecializationKind() ==
                TSK_ExplicitInstantiationDefinition &&
            inHeader(S->getPointOfInstantiation())) {
          std::string Name;
          llvm::raw_string_ostream OS(Name);
          S->getNameForDiagnostic(OS, Ctx->getPrintingPolicy(),
                                  /*Qualified=*/true);
          EmittedNames.push_back(Name);
        }
    };
    if (auto *T = dyn_cast<ClassTemplateDecl>(D))
      NoteInstantiations(T);
    if (auto *T = dyn_cast<FunctionTemplateDecl>(D))
      NoteInstantiations(T);

    if (auto *TD = dyn_cast<ClassTemplateDecl>(D)) {
      if (TD->getTemplatedDecl()->isThisDeclarationADefinition())
        visitClass(TD->getTemplatedDecl(), R, Prefix);
      else
        add(Prefix + nameOf(TD) + "#fwd", {useName(TD)}, R);
      return;
    }
    if (auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
      // Partial and explicit specializations change which definition a use
      // of the template picks, so they belong to the template's name.
      if (Spec->getSpecializationKind() == TSK_ImplicitInstantiation)
        return;
      std::string N = nameOf(Spec->getSpecializedTemplate());
      add(Prefix + N + "#spec", {useName(Spec->getSpecializedTemplate())}, R);
      return;
    }
    if (auto *RD = dyn_cast<CXXRecordDecl>(D)) {
      if (!RD->getDeclName().isIdentifier())
        return;
      if (RD->isThisDeclarationADefinition())
        visitClass(RD, R, Prefix);
      else
        add(Prefix + nameOf(RD) + "#fwd", {useName(RD)}, R);
      return;
    }
    if (auto *ED = dyn_cast<EnumDecl>(D)) {
      std::vector<std::string> Names = {useName(ED)};
      for (const EnumConstantDecl *C : ED->enumerators())
        Names.push_back(useName(C));
      if (Names[0].empty())
        Names.erase(Names.begin());
      add(Prefix + nameOf(ED) + "#enum", Names, R);
      return;
    }
    const Decl *Inner = D;
    if (auto *T = dyn_cast<TemplateDecl>(D); T && T->getTemplatedDecl())
      Inner = T->getTemplatedDecl();
    if (auto *DG = dyn_cast<CXXDeductionGuideDecl>(Inner)) {
      std::string N = nameOf(DG->getDeducedTemplate());
      add(Prefix + N + "#guide", {useName(DG->getDeducedTemplate())}, R);
      return;
    }
    if (isa<FunctionDecl, VarDecl, TypedefNameDecl, UsingDecl, ConceptDecl,
            NamespaceAliasDecl, TemplateDecl>(Inner)) {
      std::string N = useName(cast<NamedDecl>(D));
      // Constructors, destructors, conversions and member operators defined
      // out of line are reached through their class.
      if (auto *M = dyn_cast<CXXMethodDecl>(Inner); M && N.empty())
        N = useName(M->getParent());
      // `using Base::Base` names constructors: it is about its class.
      if (auto *U = dyn_cast<UsingDecl>(Inner); U && N.empty())
        if (auto *RD = dyn_cast<CXXRecordDecl>(U->getDeclContext()))
          N = useName(RD);
      std::string Key = Prefix +
                        cast<NamedDecl>(D)->getDeclName().getAsString() + "#" +
                        D->getDeclKindName();
      std::vector<std::string> Names;
      if (!N.empty())
        Names.push_back(N);
      std::vector<std::pair<unsigned, unsigned>> Holes;
      if (auto Hole = addBody(D, Key))
        Holes.push_back(*Hole);
      add(Key, Names, R, Holes);
      return;
    }
  }

public:
  explicit IndexConsumer(Index &Idx) : Idx(Idx) {}

  void HandleTranslationUnit(ASTContext &Ctx) override {
    if (Idx.FID.isInvalid())
      return;
    SM = &Ctx.getSourceManager();
    LO = &Ctx.getLangOpts();
    this->Ctx = &Ctx;
    LocationUseFinder(Idx, *SM).TraverseDecl(Ctx.getTranslationUnitDecl());
    GraphBuilder G(Idx.Graph, *SM);
    for (const Decl *D : Ctx.getTranslationUnitDecl()->decls())
      G.visit(D, "");

    // Entities depend on the macros expanded inside them, and the macros
    // expanded in #if and #elif conditions are tested ones.
    for (auto &[FID, V] : Idx.Expansions)
      llvm::sort(V);
    auto ForEachExpansion = [&](SourceRange R, auto &&F) {
      SourceLocation B = SM->getExpansionLoc(R.getBegin());
      SourceLocation E = SM->getExpansionLoc(R.getEnd());
      if (B.isInvalid() || E.isInvalid())
        return;
      auto [FB, OB] = SM->getDecomposedLoc(B);
      auto [FE, OE] = SM->getDecomposedLoc(E);
      auto It = Idx.Expansions.find(FB);
      if (FB != FE || It == Idx.Expansions.end())
        return;
      for (auto I =
               llvm::lower_bound(It->second, std::make_pair(OB, std::string()));
           I != It->second.end() && I->first <= OE; ++I)
        F(I->second);
    };
    for (auto &[Q, Rs] : G.Ranges)
      for (CharSourceRange R : Rs)
        ForEachExpansion(R.getAsRange(), [&](const std::string &M) {
          Idx.Graph[Q].Refs.insert("%" + M);
        });
    for (SourceRange R : Idx.ConditionRanges)
      ForEachExpansion(R, [&](const std::string &M) {
        // Expansions are recorded by definition; conditions by name.
        Idx.ConditionMacros.insert(M.substr(0, M.find('@')));
      });
    for (const Decl *D : Ctx.getTranslationUnitDecl()->decls())
      visitDecl(D, "");
    // They change every dependent, so they form one nameless entity.
    if (!Emitted.empty() || !EmittedNames.empty()) {
      BLAKE3 H;
      for (auto [B, E] : Emitted)
        H.update(hashRange(B, E, {}));
      llvm::sort(EmittedNames);
      for (const std::string &N : EmittedNames)
        H.update(N);
      Entity Ent;
      Ent.Key = "<emitted by every includer>";
      Ent.Nameless = true;
      Ent.Hash = toHex(H.final(), /*LowerCase=*/true);
      Idx.Entities.push_back(std::move(Ent));
    }
    StringRef Buf = SM->getBufferData(Idx.FID);
    Idx.Rest = hashRange(0, Buf.size(), Idx.Claimed);
  }
};

class IndexAction : public ASTFrontendAction {
  Index &Idx;

public:
  explicit IndexAction(Index &Idx) : Idx(Idx) {}

  bool BeginInvocation(CompilerInstance &CI) override {
    // Parse the header's text rather than a PCH built from it: after an edit
    // the PCH still holds the old contents. The PCH's source header stays in
    // the -include list, so the same declarations are seen.
    CI.getPreprocessorOpts().ImplicitPCHInclude.clear();
    // Likewise for modules: an import would hide the header's tokens and
    // macro definitions, which the index is made from.
    // All of -fmodules has to go, or headers are parsed half as modules.
    CI.getLangOpts().Modules = false;
    CI.getLangOpts().ImplicitModules = false;
    CI.getLangOpts().ModulesLocalVisibility = false;
    CI.getHeaderSearchOpts().ImplicitModuleMaps = false;
    CI.getHeaderSearchOpts().ModuleCachePath.clear();
    CI.getFrontendOpts().ModuleMapFiles.clear();
    CI.getDependencyOutputOpts() = DependencyOutputOptions();
    return true;
  }
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef) override {
    Idx.DebugInfo =
        CI.getCodeGenOpts().getDebugInfo() != llvm::codegenoptions::NoDebugInfo;
    CI.getPreprocessor().addPPCallbacks(std::make_unique<Collector>(
        Idx, CI.getSourceManager(), CI.getPreprocessor()));
    return std::make_unique<IndexConsumer>(Idx);
  }
};

} // namespace

int main(int argc, const char **argv) {
  cl::HideUnrelatedOptions(Category);
  cl::ParseCommandLineOptions(argc, argv, "List a header's named entities\n");
  if (sys::fs::real_path(Header, HeaderRealPath))
    HeaderRealPath = Header;

  std::string Err;
  auto DB = tooling::CompilationDatabase::loadFromDirectory(BuildDir, Err);
  if (!DB) {
    errs() << "error: " << Err << "\n";
    return 1;
  }
  tooling::ClangTool Tool(*DB, {Source});
  std::unique_ptr<MemoryBuffer> Old;
  if (!OldContent.empty()) {
    auto B = MemoryBuffer::getFile(OldContent);
    if (!B) {
      errs() << "error: cannot read " << OldContent << "\n";
      return 1;
    }
    Old = std::move(*B);
    // The main file includes the header by its real path, so map that one.
    Tool.mapVirtualFile(HeaderRealPath, Old->getBuffer());
  }
  // The source only lends its compile command: what it includes itself may
  // not reach the header (with modules, a depfile lists every header of each
  // imported module), so it is replaced by an include of the header alone.
  std::string Main = "#include \"" + HeaderRealPath.str().str() + "\"\n";
  if (!InSource)
    Tool.mapVirtualFile(Source, Main);

  Index Idx;
  struct Factory : tooling::FrontendActionFactory {
    Index &Idx;
    Factory(Index &Idx) : Idx(Idx) {}
    std::unique_ptr<FrontendAction> create() override {
      return std::make_unique<IndexAction>(Idx);
    }
  } F(Idx);
  int RC = Tool.run(&F);

  json::Array Ents;
  for (const Entity &E : Idx.Entities)
    Ents.push_back(json::Object{{"key", E.Key},
                                {"names", json::Array(E.Names)},
                                {"nameless", E.Nameless},
                                {"body", E.Body},
                                {"hash", E.Hash}});
  json::Object Graph;
  for (auto &[Q, N] : Idx.Graph) {
    std::set<std::string> Names(N.Names.begin(), N.Names.end());
    json::Object O{{"refs", json::Array(N.Refs)},
                   {"names", json::Array(Names)}};
    if (!N.BodyName.empty())
      O["body"] = N.BodyName;
    Graph[Q] = std::move(O);
  }
  json::Object Macros;
  for (auto &[N, H] : Idx.Macros)
    Macros[N] = H;
  json::Object Out{{"header", Header.getValue()},
                   {"found", Idx.FID.isValid()},
                   {"errors", RC != 0},
                   {"counter", Idx.UsesCounter},
                   {"rest", Idx.Rest},
                   {"macros", std::move(Macros)},
                   {"entities", std::move(Ents)},
                   {"conditionMacros", json::Array(Idx.ConditionMacros)},
                   {"emittedByEveryIncluder", json::Array(Idx.EmittedDecls)},
                   {"graph", std::move(Graph)}};
  outs() << formatv("{0:2}", json::Value(std::move(Out))) << "\n";
  return 0;
}
