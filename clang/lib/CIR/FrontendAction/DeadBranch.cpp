//===--- DeadBranch.cpp - Remove the branch a constant condition skips ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An `if` whose condition is a constant expression: the branch it never takes
// is removed, and the one it takes stays as a block of its own (its braces
// keep the scope of what it declares). Codegen already folds such a
// condition and emits nothing for a dead branch without labels, so the IR is
// unchanged and the identical-IR proof applies.
//
// A constant is only taken as dead code when nothing makes it depend on the
// build configuration: no macro in the condition, no variable whose
// initializer uses a macro or sits inside #if, and no sizeof, alignof,
// offsetof or type trait (constant for this target only). The edit keeps the
// line count, so __LINE__ and debug locations after it do not move. Opt-in,
// since it deletes code someone wrote.
//
//===----------------------------------------------------------------------===//

#include "CIRModernize.h"
#include "CIRModernizeFramework.h"

#include "clang/Basic/CharInfo.h"
#include "clang/Lex/Lexer.h"

using namespace clang;
using namespace clang::cirmodernize;

namespace {

class DeadBranch : public Rewrite {
public:
  using Rewrite::Rewrite;

  StringRef name() const override { return "dead-branch"; }
  unsigned diagID(DiagnosticsEngine &Diags) const override {
    return Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                 "this if in %1 has a constant condition; the "
                                 "branch it never takes can go");
  }
  StringRef whyNeedBlocked() const override { return "dead-branch: needed"; }
  bool changesType() const override { return false; }

  void analyze(mlir::ModuleOp) override {}

  void addDefined(FunctionDecl *FD) override {
    if (!FD->hasBody() || FD->isTemplated() || FD->isDependentContext())
      return;
    unsigned Ordinal = 0;
    walk(FD, FD->getBody(), Ordinal);
  }

  // The statements are in the body, which only the defining TU has.
  void addExternal(FunctionDecl *, unsigned) override {}

private:
  void walk(FunctionDecl *FD, Stmt *S, unsigned &Ordinal) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (auto *If = dyn_cast<IfStmt>(S))
      if (constantIf(FD, If, Ordinal))
        return;
    for (Stmt *Child : S->children())
      walk(FD, Child, Ordinal);
  }

  // Whether If was taken as a candidate (its branches are then not walked:
  // one of them goes).
  bool constantIf(FunctionDecl *FD, IfStmt *If, unsigned &Ordinal) {
    const Expr *Cond = If->getCond();
    bool Value;
    if (If->isConstexpr() || If->isConsteval() || If->getInit() ||
        If->getConditionVariable() || !Cond || Cond->isValueDependent() ||
        Cond->HasSideEffects(Ctx.C) ||
        !Cond->EvaluateAsBooleanCondition(Value, Ctx.C))
      return false;
    Ctx.count("dead-branch: constant conditions");
    Candidate Cand{FD, nullptr, Ordinal++, Id};
    Cand.Loc = If->getIfLoc();
    Stmt *Live = Value ? If->getThen() : If->getElse();
    Stmt *Dead = Value ? If->getElse() : If->getThen();
    StringRef Why = whyNot(If, Cond, Dead);
    if (Why.empty() && !edits(Cand, If, Live))
      Why = "dead-branch: the if is in a macro";
    Ctx.add(std::move(Cand), Why);
    return true;
  }

  StringRef whyNot(const IfStmt *If, const Expr *Cond, const Stmt *Dead) {
    if (!isa<CompoundStmt>(If->getThen()) ||
        (If->getElse() && !isa<CompoundStmt, IfStmt>(If->getElse())))
      return "dead-branch: a branch without braces";
    if (spelledByMacro(Cond))
      return "dead-branch: the condition comes from a macro";
    if (targetDependent(Cond))
      return "dead-branch: the condition depends on the target";
    if (StringRef Why = whyConfigVariable(Cond); !Why.empty())
      return Why;
    // Codegen emits a dead branch that holds a label, which a goto or a
    // case can still reach.
    if (Dead && hasLabel(Dead))
      return "dead-branch: the dead branch holds a label";
    return {};
  }

  static bool spelledByMacro(const Stmt *S) {
    if (S->getBeginLoc().isMacroID() || S->getEndLoc().isMacroID())
      return true;
    for (const Stmt *Child : S->children())
      if (Child && spelledByMacro(Child))
        return true;
    return false;
  }

  // A variable the condition reads may be a configuration flag: refused when
  // its initializer uses a macro or it is defined inside #if.
  StringRef whyConfigVariable(const Stmt *S) {
    if (const auto *DRE = dyn_cast<DeclRefExpr>(S))
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        const VarDecl *Def = VD->getDefinition();
        const Expr *Init = Def ? Def->getInit() : VD->getInit();
        if (Init && spelledByMacro(Init))
          return "dead-branch: the condition reads a variable a macro sets";
        const ConditionalRecord *CR = Ctx.Options.Conditionals;
        if (CR && CR->overlaps((Def ? Def : VD)->getSourceRange()))
          return "dead-branch: the condition reads a variable defined in #if";
      }
    for (const Stmt *Child : S->children())
      if (Child)
        if (StringRef Why = whyConfigVariable(Child); !Why.empty())
          return Why;
    return {};
  }

  static bool targetDependent(const Stmt *S) {
    if (isa<UnaryExprOrTypeTraitExpr, OffsetOfExpr, TypeTraitExpr,
            ArrayTypeTraitExpr, ExpressionTraitExpr>(S))
      return true;
    for (const Stmt *Child : S->children())
      if (Child && targetDependent(Child))
        return true;
    return false;
  }

  static bool hasLabel(const Stmt *S) {
    if (isa<LabelStmt, SwitchCase>(S))
      return true;
    for (const Stmt *Child : S->children())
      if (Child && hasLabel(Child))
        return true;
    return false;
  }

  // The whole if becomes the live branch's text; with no live branch, the
  // lines it takes go.
  bool edits(Candidate &Cand, const IfStmt *If, const Stmt *Live) {
    const SourceManager &SM = Ctx.SM;
    const LangOptions &LO = Ctx.C.getLangOpts();
    SourceLocation Begin = If->getBeginLoc(), End = If->getEndLoc();
    if (Begin.isMacroID() || End.isMacroID())
      return false;
    End = Lexer::getLocForEndOfToken(End, 0, SM, LO);
    if (Live) {
      bool Invalid = false;
      StringRef Text = Lexer::getSourceText(
          CharSourceRange::getTokenRange(Live->getSourceRange()), SM, LO,
          &Invalid);
      if (Invalid || Live->getBeginLoc().isMacroID())
        return false;
      Cand.Edits.push_back(FixItHint::CreateReplacement(
          CharSourceRange::getCharRange(Begin, End),
          keepLines(CharSourceRange::getCharRange(Begin, End), Text)));
      return true;
    }
    CharSourceRange Lines = wholeLines(Begin, End);
    Cand.Edits.push_back(
        FixItHint::CreateReplacement(Lines, keepLines(Lines, "")));
    return true;
  }

  // Text, followed by as many newlines as the range loses.
  std::string keepLines(CharSourceRange R, StringRef Text) {
    StringRef Old(Ctx.SM.getCharacterData(R.getBegin()),
                  Ctx.SM.getFileOffset(R.getEnd()) -
                      Ctx.SM.getFileOffset(R.getBegin()));
    size_t Lost = Old.count('\n') - std::min(Old.count('\n'), Text.count('\n'));
    return (Text + std::string(Lost, '\n')).str();
  }

  // [Begin, End) widened to whole lines when nothing else shares them.
  CharSourceRange wholeLines(SourceLocation Begin, SourceLocation End) {
    const char *B = Ctx.SM.getCharacterData(Begin);
    const char *E = Ctx.SM.getCharacterData(End);
    const char *LineBegin = B;
    while (isHorizontalWhitespace(LineBegin[-1]))
      --LineBegin;
    const char *LineEnd = E;
    while (isHorizontalWhitespace(*LineEnd))
      ++LineEnd;
    if (!isVerticalWhitespace(LineBegin[-1]) || !isVerticalWhitespace(*LineEnd))
      return CharSourceRange::getCharRange(Begin, End);
    return CharSourceRange::getCharRange(Begin.getLocWithOffset(LineBegin - B),
                                         End.getLocWithOffset(LineEnd + 1 - E));
  }
};

} // namespace

std::unique_ptr<Rewrite> clang::cirmodernize::createDeadBranch(Context &Ctx,
                                                               unsigned Id) {
  return std::make_unique<DeadBranch>(Ctx, Id);
}
