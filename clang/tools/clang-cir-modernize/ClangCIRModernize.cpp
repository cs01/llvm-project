//===--- ClangCIRModernize.cpp - Proven C++ rewrites from ClangIR ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// clang-cir-modernize runs the clangCIRModernize rewrites over files from a
// compilation database, the way clang-tidy runs its checks: it builds each
// translation unit's CIR module in memory (never printed), runs the rewrites,
// and prints their warnings with fix-its, or writes them in
// clang-apply-replacements YAML (--export-fixes).
//
// --fix writes only edits it has proven: it compiles the translation unit
// with classic codegen before and after the edits and compares the LLVM IR
// (IRCompare.h, as cir-modernize-ircmp does). When the edits together change
// the IR, they are bisected and the ones that change it are left out. It
// edits only the file being compiled: an edit to a header would change other
// translation units this run does not compile, which is what project.ts is
// for.
//
// The rewrites and their analyses are described in
// clang/lib/CIR/Modernize/CIRModernize.cpp.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CIR/CIRGenerator.h"
#include "clang/CIR/Modernize/CIRModernize.h"
#include "clang/CIR/Modernize/IRCompare.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/ReplacementsYaml.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <set>

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory Category("clang-cir-modernize options");

static llvm::cl::opt<bool>
    Fix("fix",
        llvm::cl::desc("Apply the edits that leave the IR unchanged, in the "
                       "files compiled (not headers)."),
        llvm::cl::cat(Category));
static llvm::cl::opt<std::string>
    ExportFixes("export-fixes",
                llvm::cl::desc("Write the edits to this file as YAML that "
                               "clang-apply-replacements reads."),
                llvm::cl::value_desc("file"), llvm::cl::cat(Category));
static llvm::cl::opt<std::string>
    Only("only",
         llvm::cl::desc("Comma-separated rewrites or groups to make; by "
                        "default the ones proven by identical IR."),
         llvm::cl::cat(Category));
static llvm::cl::opt<std::string>
    Skip("skip", llvm::cl::desc("Comma-separated rewrites or groups to skip."),
         llvm::cl::cat(Category));
static llvm::cl::opt<std::string>
    View("view", llvm::cl::desc("View type for the span rewrite: std or llvm."),
         llvm::cl::cat(Category));
// Adds a global to every edited version, so the proof must refuse all edits:
// lets a test show that it does.
static llvm::cl::opt<bool> BreakProofForTesting(
    "break-proof-for-testing", llvm::cl::Hidden,
    llvm::cl::desc("Change the IR of every edited version."));

static llvm::cl::opt<bool>
    AllowPublic("allow-public-api",
                llvm::cl::desc("Also change functions with external linkage."),
                llvm::cl::cat(Category));

namespace {

// The edits of one diagnostic, which stand or fall together.
using Edit = std::vector<Replacement>;

// Collects the fix-its of the rewrites' own diagnostics (custom IDs), also
// when a project's -Werror made them errors; clang's fix-its are left out.
class FixItCollector : public DiagnosticConsumer {
public:
  FixItCollector(DiagnosticConsumer *Next, std::vector<Edit> &Out)
      : Next(Next), Out(Out) {}

  void BeginSourceFile(const LangOptions &LO, const Preprocessor *PP) override {
    Next->BeginSourceFile(LO, PP);
  }
  void EndSourceFile() override { Next->EndSourceFile(); }

  void HandleDiagnostic(DiagnosticsEngine::Level Level,
                        const Diagnostic &Info) override {
    DiagnosticConsumer::HandleDiagnostic(Level, Info);
    Next->HandleDiagnostic(Level, Info);
    if ((Level != DiagnosticsEngine::Warning &&
         Level != DiagnosticsEngine::Error) ||
        Info.getID() < diag::DIAG_UPPER_LIMIT || !Info.hasSourceManager() ||
        Info.getFixItHints().empty())
      return;
    const SourceManager &SM = Info.getSourceManager();
    Edit E;
    for (const FixItHint &Hint : Info.getFixItHints()) {
      Replacement R(SM, Hint.RemoveRange, Hint.CodeToInsert);
      if (!R.isApplicable())
        return;
      E.push_back(std::move(R));
    }
    Out.push_back(std::move(E));
  }

private:
  DiagnosticConsumer *Next;
  std::vector<Edit> &Out;
};

class ModernizeConsumer : public ASTConsumer {
public:
  ModernizeConsumer(CompilerInstance &CI, const ConditionalRecord *Conditionals)
      : CI(CI), Conditionals(Conditionals),
        Gen(std::make_unique<cir::CIRGenerator>(CI.getDiagnostics(),
                                                &CI.getVirtualFileSystem(),
                                                CI.getCodeGenOpts())) {}

  void Initialize(ASTContext &Ctx) override { Gen->Initialize(Ctx); }
  bool HandleTopLevelDecl(DeclGroupRef D) override {
    Gen->HandleTopLevelDecl(D);
    return true;
  }
  void HandleCXXStaticMemberVarInstantiation(VarDecl *VD) override {
    Gen->HandleCXXStaticMemberVarInstantiation(VD);
  }
  void HandleInlineFunctionDefinition(FunctionDecl *D) override {
    Gen->HandleInlineFunctionDefinition(D);
  }
  void HandleTagDeclDefinition(TagDecl *D) override {
    Gen->HandleTagDeclDefinition(D);
  }
  void HandleTagDeclRequiredDefinition(const TagDecl *D) override {
    Gen->HandleTagDeclRequiredDefinition(D);
  }
  void CompleteTentativeDefinition(VarDecl *D) override {
    Gen->CompleteTentativeDefinition(D);
  }
  void HandleVTable(CXXRecordDecl *RD) override { Gen->HandleVTable(RD); }

  void HandleTranslationUnit(ASTContext &C) override {
    Gen->HandleTranslationUnit(C);
    if (CI.getDiagnostics().hasErrorOccurred() || !Gen->verifyModule())
      return;
    CIRModernizeOptions Options;
    Options.AllowPublic = AllowPublic;
    Options.Only = Only;
    Options.Skip = Skip;
    Options.View = View;
    Options.Conditionals = Conditionals;
    runCIRModernize(Gen->getModule(), C, CI.getDiagnostics(), Options);
  }

private:
  CompilerInstance &CI;
  const ConditionalRecord *Conditionals;
  std::unique_ptr<cir::CIRGenerator> Gen;
};

class ModernizeAction : public ASTFrontendAction {
public:
  explicit ModernizeAction(std::vector<Edit> &Out) : Out(Out) {}

  bool BeginInvocation(CompilerInstance &CI) override {
    CI.getLangOpts().NullabilitySafetyCIR = 1;
    return true;
  }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef) override {
    DiagnosticsEngine &Diags = CI.getDiagnostics();
    // The collector lives in this action, which goes before the diagnostics
    // engine does: put the original client back in EndSourceFileAction,
    // whether or not the engine owned it.
    Original = Diags.getClient();
    OwnedOriginal = Diags.takeClient();
    Collector = std::make_unique<FixItCollector>(Original, Out);
    Diags.setClient(Collector.get(), /*ShouldOwnClient=*/false);
    auto Record = std::make_unique<ConditionalRecord>(CI.getPreprocessor());
    const ConditionalRecord *Conditionals = Record.get();
    CI.getPreprocessor().addPPCallbacks(std::move(Record));
    return std::make_unique<ModernizeConsumer>(CI, Conditionals);
  }

  void EndSourceFileAction() override {
    DiagnosticsEngine &Diags = getCompilerInstance().getDiagnostics();
    if (OwnedOriginal)
      Diags.setClient(OwnedOriginal.release(), /*ShouldOwnClient=*/true);
    else
      Diags.setClient(Original, /*ShouldOwnClient=*/false);
  }

private:
  std::vector<Edit> &Out;
  std::unique_ptr<FixItCollector> Collector;
  DiagnosticConsumer *Original = nullptr;
  std::unique_ptr<DiagnosticConsumer> OwnedOriginal;
};

class ModernizeActionFactory : public FrontendActionFactory {
public:
  explicit ModernizeActionFactory(std::vector<Edit> &Out) : Out(Out) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<ModernizeAction>(Out);
  }

private:
  std::vector<Edit> &Out;
};

// Compiles a translation unit to LLVM IR with classic codegen and keeps the
// module.
class EmitIR : public ToolAction {
public:
  explicit EmitIR(llvm::LLVMContext &Ctx) : Ctx(Ctx) {}

  bool runInvocation(std::shared_ptr<CompilerInvocation> Invocation,
                     FileManager *Files,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                     DiagnosticConsumer *DiagConsumer) override {
    CompilerInstance Compiler(std::move(Invocation),
                              std::move(PCHContainerOps));
    Compiler.setVirtualFileSystem(Files->getVirtualFileSystemPtr());
    Compiler.setFileManager(Files);
    Compiler.createDiagnostics(DiagConsumer, /*ShouldOwnClient=*/false);
    Compiler.createSourceManager();
    EmitLLVMOnlyAction Action(&Ctx);
    if (!Compiler.ExecuteAction(Action))
      return false;
    Module = Action.takeModule();
    return Module != nullptr;
  }

  llvm::LLVMContext &Ctx;
  std::unique_ptr<llvm::Module> Module;
};

// Proves edits to one file of one translation unit by comparing its IR before
// and after them.
class Prover {
public:
  Prover(const CompilationDatabase &Compilations, std::string File,
         StringRef Code, bool Refine)
      : Compilations(Compilations), File(std::move(File)), Code(Code),
        Refine(Refine) {}

  bool compileOriginal() {
    std::optional<std::string> BC = compile(std::nullopt);
    if (!BC)
      return false;
    Original = std::move(*BC);
    return true;
  }

  // The edits that keep the IR, most of them when not all do.
  std::vector<const Edit *> prove(const std::vector<const Edit *> &Edits) {
    std::vector<const Edit *> Kept = bisect(Edits);
    // Edits that pass one by one can still fail together (one needs another
    // that was left out); then none is made.
    if (Kept.size() != Edits.size() && !Kept.empty() && !keepsIR(Kept))
      Kept.clear();
    return Kept;
  }

  std::optional<std::string> apply(const std::vector<const Edit *> &Edits) {
    Replacements Rs;
    for (const Edit *E : Edits)
      for (const Replacement &R : *E)
        if (llvm::Error Err = Rs.add(R)) {
          llvm::consumeError(std::move(Err));
          return std::nullopt;
        }
    llvm::Expected<std::string> New = applyAllReplacements(Code, Rs);
    if (!New) {
      llvm::consumeError(New.takeError());
      return std::nullopt;
    }
    return *New;
  }

  std::string LastWhy;

private:
  std::vector<const Edit *> bisect(const std::vector<const Edit *> &Edits) {
    if (Edits.empty() || keepsIR(Edits))
      return Edits;
    if (Edits.size() == 1)
      return {};
    size_t Half = Edits.size() / 2;
    std::vector<const Edit *> Kept =
        bisect({Edits.begin(), Edits.begin() + Half});
    std::vector<const Edit *> Right =
        bisect({Edits.begin() + Half, Edits.end()});
    Kept.insert(Kept.end(), Right.begin(), Right.end());
    return Kept;
  }

  bool keepsIR(const std::vector<const Edit *> &Edits) {
    std::optional<std::string> New = apply(Edits);
    if (!New)
      return false;
    if (BreakProofForTesting)
      *New += "\nint cir_modernize_break_proof;\n";
    std::optional<std::string> NextBC = compile(*New);
    if (!NextBC) {
      LastWhy = "the edited file does not compile";
      return false;
    }
    // Each module gets its own context: comparing renames struct types,
    // which belong to the context.
    llvm::LLVMContext OrigCtx, NextCtx;
    auto Orig =
        llvm::parseBitcodeFile(llvm::MemoryBufferRef(Original, File), OrigCtx);
    auto Next =
        llvm::parseBitcodeFile(llvm::MemoryBufferRef(*NextBC, File), NextCtx);
    if (!Orig || !Next) {
      LastWhy = "cannot read back the bitcode";
      llvm::consumeError(Orig.takeError());
      llvm::consumeError(Next.takeError());
      return false;
    }
    return cirmodernize::compareModules(**Orig, **Next, Refine, LastWhy) ==
           cirmodernize::IRMatch::Match;
  }

  // The module as bitcode, which is how cir-modernize-ircmp reads both sides:
  // writing and reading it back is part of what the comparison expects (it
  // orders metadata, for one).
  std::optional<std::string> compile(std::optional<StringRef> Contents) {
    ClangTool Tool(Compilations, {File});
    IgnoringDiagConsumer Quiet;
    Tool.setDiagnosticConsumer(&Quiet);
    // As the ircmp check compiles: -O0 for identical IR, or unoptimized
    // without optnone for --refine.
    std::vector<std::string> Flags =
        Refine ? std::vector<std::string>{"-O1", "-Xclang",
                                          "-disable-llvm-passes", "-g0"}
               : std::vector<std::string>{"-O0", "-g0"};
    Flags.push_back("-Wno-everything");
    Flags.push_back("-Wno-error");
    Tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster(Flags, ArgumentInsertPosition::END));
    if (Contents)
      Tool.mapVirtualFile(File, *Contents);
    llvm::LLVMContext Ctx;
    EmitIR Action(Ctx);
    if (Tool.run(&Action) != 0)
      return std::nullopt;
    std::string BC;
    llvm::raw_string_ostream OS(BC);
    // Keep use-list order: the printer lists a block's predecessors in it.
    llvm::WriteBitcodeToFile(*Action.Module, OS,
                             /*ShouldPreserveUseListOrder=*/true);
    return BC;
  }

  const CompilationDatabase &Compilations;
  std::string File;
  StringRef Code;
  bool Refine;
  // The unedited module, as bitcode.
  std::string Original;
};

bool sameFile(StringRef A, StringRef B) {
  return A == B || llvm::sys::fs::equivalent(A, B);
}

ArgumentsAdjuster rewriteFlags() {
  return getInsertArgumentAdjuster({"-Wno-everything", "-Wno-error"},
                                   ArgumentInsertPosition::END);
}

// Proves and writes the edits for one file; returns false on an I/O error.
bool fixFile(const CompilationDatabase &Compilations, const std::string &File,
             std::vector<Edit> &All) {
  std::vector<Edit> Edits;
  {
    ClangTool Tool(Compilations, {File});
    Tool.appendArgumentsAdjuster(rewriteFlags());
    ModernizeActionFactory Factory(Edits);
    Tool.run(&Factory);
  }
  All.insert(All.end(), Edits.begin(), Edits.end());

  std::vector<const Edit *> Here;
  unsigned InHeaders = 0;
  for (const Edit &E : Edits) {
    if (llvm::all_of(E, [&](const Replacement &R) {
          return sameFile(R.getFilePath(), File);
        }))
      Here.push_back(&E);
    else
      ++InHeaders;
  }
  if (Here.empty()) {
    if (InHeaders)
      llvm::errs() << File << ": " << InHeaders
                   << " edits left out: they change a header\n";
    return true;
  }

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
      llvm::MemoryBuffer::getFile(File);
  if (!Buffer) {
    llvm::errs() << "cannot read " << File << ": "
                 << Buffer.getError().message() << "\n";
    return false;
  }
  std::string Code = (*Buffer)->getBuffer().str();
  bool Refine = cirmodernize::selectsRefinement(Only, Skip);
  Prover P(Compilations, File, Code, Refine);
  if (!P.compileOriginal()) {
    llvm::errs() << File << ": not changed: it does not compile with classic "
                 << "codegen\n";
    return true;
  }
  std::vector<const Edit *> Kept = P.prove(Here);
  std::optional<std::string> New = P.apply(Kept);
  if (!Kept.empty() && New) {
    std::error_code EC;
    llvm::raw_fd_ostream OS(File, EC);
    if (EC) {
      llvm::errs() << "cannot write " << File << ": " << EC.message() << "\n";
      return false;
    }
    OS << *New;
  }
  llvm::errs() << File << ": " << Kept.size() << " edits applied";
  if (size_t Refused = Here.size() - Kept.size())
    llvm::errs() << ", " << Refused << " left out: they change the IR ("
                 << P.LastWhy << ")";
  if (InHeaders)
    llvm::errs() << ", " << InHeaders << " left out: they change a header";
  llvm::errs() << "\n";
  return true;
}

} // namespace

int main(int argc, const char **argv) {
  llvm::InitLLVM X(argc, argv);
  auto Parser = CommonOptionsParser::create(argc, argv, Category);
  if (!Parser) {
    llvm::errs() << llvm::toString(Parser.takeError());
    return 1;
  }

  std::vector<Edit> Edits;
  int Status = 0;
  if (Fix) {
    for (const std::string &Path : Parser->getSourcePathList()) {
      std::string File = getAbsolutePath(Path);
      if (!fixFile(Parser->getCompilations(), File, Edits))
        Status = 1;
    }
  } else {
    ClangTool Tool(Parser->getCompilations(), Parser->getSourcePathList());
    Tool.appendArgumentsAdjuster(rewriteFlags());
    ModernizeActionFactory Factory(Edits);
    Status = Tool.run(&Factory);
  }

  if (!ExportFixes.empty()) {
    TranslationUnitReplacements TUR;
    std::set<Replacement> Sorted;
    for (const Edit &E : Edits)
      Sorted.insert(E.begin(), E.end());
    TUR.Replacements.assign(Sorted.begin(), Sorted.end());
    std::error_code EC;
    llvm::raw_fd_ostream OS(ExportFixes, EC);
    if (EC) {
      llvm::errs() << "cannot write " << ExportFixes << ": " << EC.message()
                   << "\n";
      return 1;
    }
    llvm::yaml::Output YAML(OS);
    YAML << TUR;
  }
  return Status;
}
