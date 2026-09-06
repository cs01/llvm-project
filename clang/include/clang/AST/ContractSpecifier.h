//===- ContractSpecifier.h - C contract clauses -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the contract clauses ('pre', 'post', 'writes') that -fc-contracts
/// attaches to a function declaration.
///
/// Contracts hang off the FunctionDecl, not off its type. Two declarations of
/// the same function type may carry different contracts, and a contract must
/// never participate in type identity, overload resolution, or mangling.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_CONTRACTSPECIFIER_H
#define LLVM_CLANG_AST_CONTRACTSPECIFIER_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ErrorHandling.h"

namespace clang {

class ASTContext;
class Expr;
class VarDecl;

/// A single contract clause, e.g. `requires (dst != 0)`.
class ContractClause {
public:
  enum ClauseKind {
    /// A precondition, checked on entry to the callee.
    CK_Requires,
    /// A postcondition, checked at every return.
    CK_Ensures,
    /// A frame condition. Not yet parsed.
    CK_Assigns,
    /// A loop invariant: holds at every iteration of the loop it is attached
    /// to.
    CK_LoopInvariant,
    /// A termination measure: a non-negative quantity that strictly decreases
    /// each iteration, which is what makes the loop provably terminate.
    CK_Decreases,
  };

private:
  ClauseKind Kind;
  SourceLocation KeywordLoc;
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;

  /// The predicate. Null only when the predicate failed to parse or to
  /// type-check, so that one bad clause does not discard the others.
  Expr *Predicate;

  /// For `ensures (r: ...)`, the binding for the return value. Null when the
  /// clause named no result, and always null for a 'requires'.
  VarDecl *ResultVar = nullptr;

  /// For 'assigns', the frame targets: the locations the function may modify.
  /// Null for every other kind.
  ///
  /// A frame condition is a *set of locations*, not a predicate, which is why
  /// it cannot reuse Predicate: a set has no truth value and, per section 4 of
  /// the design, cannot be disjoined either. Allocated in the ASTContext
  /// alongside the clause array.
  Expr **Targets = nullptr;
  unsigned NumTargets = 0;

public:
  ContractClause() = default;
  ContractClause(ClauseKind Kind, SourceLocation KeywordLoc,
                 SourceLocation LParenLoc, SourceLocation RParenLoc,
                 Expr *Predicate)
      : Kind(Kind), KeywordLoc(KeywordLoc), LParenLoc(LParenLoc),
        RParenLoc(RParenLoc), Predicate(Predicate) {}

  ClauseKind getKind() const { return Kind; }
  SourceLocation getKeywordLoc() const { return KeywordLoc; }
  SourceLocation getLParenLoc() const { return LParenLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceRange getSourceRange() const { return {KeywordLoc, RParenLoc}; }

  Expr *getPredicate() const { return Predicate; }
  void setPredicate(Expr *E) { Predicate = E; }

  VarDecl *getResultVar() const { return ResultVar; }
  void setResultVar(VarDecl *VD) { ResultVar = VD; }

  ArrayRef<Expr *> getTargets() const { return {Targets, NumTargets}; }
  void setTargets(Expr **Ts, unsigned N) {
    Targets = Ts;
    NumTargets = N;
  }

  /// An 'assigns' carries targets rather than a predicate, so it is valid when
  /// it has them. Every other kind needs a predicate.
  bool isInvalid() const {
    if (Kind == CK_Assigns)
      return Targets == nullptr;
    return Predicate == nullptr;
  }

  /// The spelling of \p Kind, for diagnostics and AST dumps.
  static const char *getKindSpelling(ClauseKind Kind) {
    switch (Kind) {
    case CK_Requires:
      return "requires";
    case CK_Ensures:
      return "ensures";
    case CK_Assigns:
      return "assigns";
    case CK_LoopInvariant:
      return "loop_invariant";
    case CK_Decreases:
      return "decreases";
    }
    llvm_unreachable("unhandled contract clause kind");
  }
  const char *getKindSpelling() const { return getKindSpelling(Kind); }
};

/// The full sequence of contract clauses on one function declaration, in
/// source order. Order is significant: clauses conjoin left to right, so a
/// later clause may rely on an earlier one having held (`pre (p != 0)` before
/// `pre (p->len > 0)`).
class ContractSpecifier final {
  ContractClause *Clauses;
  unsigned NumClauses;

  ContractSpecifier(ContractClause *Clauses, unsigned NumClauses)
      : Clauses(Clauses), NumClauses(NumClauses) {}

public:
  /// Allocates a specifier and a copy of \p Clauses in \p Ctx. Returns null
  /// for an empty clause list so that "has contracts" is a null check.
  static ContractSpecifier *Create(const ASTContext &Ctx,
                                   ArrayRef<ContractClause> Clauses);

  unsigned size() const { return NumClauses; }
  bool empty() const { return NumClauses == 0; }

  ArrayRef<ContractClause> clauses() const { return {Clauses, NumClauses}; }
  MutableArrayRef<ContractClause> clauses() { return {Clauses, NumClauses}; }

  using iterator = ContractClause *;
  using const_iterator = const ContractClause *;
  iterator begin() { return Clauses; }
  iterator end() { return Clauses + NumClauses; }
  const_iterator begin() const { return Clauses; }
  const_iterator end() const { return Clauses + NumClauses; }

  SourceRange getSourceRange() const {
    if (NumClauses == 0)
      return {};
    return {Clauses[0].getKeywordLoc(), Clauses[NumClauses - 1].getRParenLoc()};
  }
};

} // namespace clang

#endif // LLVM_CLANG_AST_CONTRACTSPECIFIER_H
