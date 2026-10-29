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
// and prints their warnings with fix-its, applies them (--fix), or writes them
// in clang-apply-replacements YAML (--export-fixes).
//
// The rewrites, their analyses and how to check an edit independently are
// described in clang/lib/CIR/Modernize/CIRModernize.cpp.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CIR/CIRGenerator.h"
#include "clang/CIR/Modernize/CIRModernize.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/ReplacementsYaml.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <set>

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory Category("clang-cir-modernize options");

static llvm::cl::opt<bool> Fix("fix", llvm::cl::desc("Apply the edits."),
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
static llvm::cl::opt<bool>
    AllowPublic("allow-public-api",
                llvm::cl::desc("Also change functions with external linkage."),
                llvm::cl::cat(Category));

namespace {

class FixItCollector : public DiagnosticConsumer {
public:
  FixItCollector(DiagnosticConsumer *Next,
                 std::map<std::string, std::set<Replacement>> &Out)
      : Next(Next), Out(Out) {}

  void BeginSourceFile(const LangOptions &LO, const Preprocessor *PP) override {
    Next->BeginSourceFile(LO, PP);
  }
  void EndSourceFile() override { Next->EndSourceFile(); }

  void HandleDiagnostic(DiagnosticsEngine::Level Level,
                        const Diagnostic &Info) override {
    DiagnosticConsumer::HandleDiagnostic(Level, Info);
    Next->HandleDiagnostic(Level, Info);
    if (Level != DiagnosticsEngine::Warning || !Info.hasSourceManager())
      return;
    const SourceManager &SM = Info.getSourceManager();
    for (const FixItHint &Hint : Info.getFixItHints()) {
      Replacement R(SM, Hint.RemoveRange, Hint.CodeToInsert);
      if (R.isApplicable())
        Out[std::string(R.getFilePath())].insert(R);
    }
  }

private:
  DiagnosticConsumer *Next;
  std::map<std::string, std::set<Replacement>> &Out;
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
    Options.Apply = Fix;
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
  explicit ModernizeAction(std::map<std::string, std::set<Replacement>> &Out)
      : Out(Out) {}

  bool BeginInvocation(CompilerInstance &CI) override {
    CI.getLangOpts().NullabilitySafetyCIR = 1;
    return true;
  }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef) override {
    DiagnosticsEngine &Diags = CI.getDiagnostics();
    Collector = std::make_unique<FixItCollector>(Diags.getClient(), Out);
    Owner = Diags.takeClient();
    Diags.setClient(Collector.get(), /*ShouldOwnClient=*/false);
    auto Record = std::make_unique<ConditionalRecord>(CI.getPreprocessor());
    const ConditionalRecord *Conditionals = Record.get();
    CI.getPreprocessor().addPPCallbacks(std::move(Record));
    return std::make_unique<ModernizeConsumer>(CI, Conditionals);
  }

  void EndSourceFileAction() override {
    DiagnosticsEngine &Diags = getCompilerInstance().getDiagnostics();
    DiagnosticConsumer *Original = Owner ? Owner.release() : nullptr;
    if (Original)
      Diags.setClient(Original, /*ShouldOwnClient=*/true);
  }

private:
  std::map<std::string, std::set<Replacement>> &Out;
  std::unique_ptr<FixItCollector> Collector;
  std::unique_ptr<DiagnosticConsumer> Owner;
};

class ModernizeActionFactory : public FrontendActionFactory {
public:
  explicit ModernizeActionFactory(
      std::map<std::string, std::set<Replacement>> &Out)
      : Out(Out) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<ModernizeAction>(Out);
  }

private:
  std::map<std::string, std::set<Replacement>> &Out;
};

} // namespace

int main(int argc, const char **argv) {
  llvm::InitLLVM X(argc, argv);
  auto Parser = CommonOptionsParser::create(argc, argv, Category);
  if (!Parser) {
    llvm::errs() << llvm::toString(Parser.takeError());
    return 1;
  }
  ClangTool Tool(Parser->getCompilations(), Parser->getSourcePathList());
  Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
      "-Wno-everything", ArgumentInsertPosition::END));

  std::map<std::string, std::set<Replacement>> Edits;
  ModernizeActionFactory Factory(Edits);
  int Status = Tool.run(&Factory);

  if (!ExportFixes.empty()) {
    TranslationUnitReplacements TUR;
    for (const auto &[File, Rs] : Edits)
      TUR.Replacements.insert(TUR.Replacements.end(), Rs.begin(), Rs.end());
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
