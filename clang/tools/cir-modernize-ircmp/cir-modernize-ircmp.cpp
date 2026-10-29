//===- cir-modernize-ircmp.cpp - Prove a rewrite left the IR unchanged ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Compares the -O0 IR of a translation unit before and after cir-modernize
// rewrote it. The rewrites change only spelling, but three things in the IR
// follow the spelling, and only those are undone before the modules must
// print identically:
//
//  * pointer to reference (P4Node -> R4Node), pointer to const
//    (P4Node -> PK4Node) and const method (_ZN -> _ZNK) change mangled names.
//    Globals are paired by position and must demangle alike once '&' reads
//    as '*' and `const` before a '*' or after a parameter list is dropped; the
//    rewritten module then takes the original's names, also where a name is
//    embedded in another (__func__._ZL5firstP4Node) or names a COMDAT.
//  * a reference parameter or argument carries nonnull, align and
//    dereferenceable, and a load of a reference carries !nonnull, !align and
//    !noundef. Both are stripped from both modules, and metadata is
//    renumbered by first use, so nodes only those tags named disappear.
//  * named struct types get suffixes in creation order (ArrayRef.1198), which
//    shift when an unrelated type is created first; each name, suffix
//    dropped, is renumbered by order of first use.
//
// Exits 0 when the modules match, 1 with the first differing line otherwise.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

using namespace llvm;

static cl::opt<std::string> OrigPath(cl::Positional, cl::Required,
                                     cl::desc("<original.ll>"));
static cl::opt<std::string> NextPath(cl::Positional, cl::Required,
                                     cl::desc("<rewritten.ll>"));
static cl::opt<bool> Print("print",
                           cl::desc("print both modules as compared"));

static AttributeList stripParamFacts(LLVMContext &C, AttributeList AL,
                                     unsigned NumParams) {
  AttributeMask Mask;
  Mask.addAttribute(Attribute::NonNull)
      .addAttribute(Attribute::Alignment)
      .addAttribute(Attribute::Dereferenceable)
      .addAttribute(Attribute::DereferenceableOrNull);
  for (unsigned I = 0; I < NumParams; ++I)
    AL = AL.removeParamAttributes(C, I, Mask);
  return AL;
}

static void stripRefFacts(Module &M) {
  LLVMContext &C = M.getContext();
  for (Function &F : M) {
    F.setAttributes(stripParamFacts(C, F.getAttributes(), F.arg_size()));
    for (Instruction &I : instructions(F)) {
      if (auto *CB = dyn_cast<CallBase>(&I))
        CB->setAttributes(
            stripParamFacts(C, CB->getAttributes(), CB->arg_size()));
      else if (auto *LI = dyn_cast<LoadInst>(&I); LI && LI->getType()->isPointerTy())
        for (unsigned Kind : {LLVMContext::MD_nonnull, LLVMContext::MD_align,
                              LLVMContext::MD_noundef})
          LI->setMetadata(Kind, nullptr);
    }
  }
}

// What a rewrite may change in a mangled name, undone. llvm::demangle returns
// some names (generic lambdas) unchanged; then the same change is undone on
// the mangled form: R and P, each optionally followed by K, all read as P.
static std::string signature(StringRef Mangled) {
  std::string D = demangle(Mangled);
  std::string Out;
  if (D == Mangled) {
    for (size_t I = 0; I < Mangled.size(); ++I) {
      char Ch = Mangled[I];
      if (Ch != 'P' && Ch != 'R') {
        Out += Ch;
        continue;
      }
      Out += 'P';
      if (I + 1 < Mangled.size() && Mangled[I + 1] == 'K')
        ++I;
    }
    // NK: a const method's nested name.
    for (size_t I; (I = Out.find("NK")) != std::string::npos;)
      Out.erase(I + 1, 1);
    return Out;
  }
  // '&' first, so the combined rewrite (Node const&) reads as Node*.
  std::replace(D.begin(), D.end(), '&', '*');
  for (size_t I = 0; I < D.size(); ++I) {
    if (StringRef(D).substr(I).starts_with(" const*"))
      I += 6;
    Out += D[I];
  }
  // A method's own const, after its parameter list.
  for (size_t I = 0; (I = Out.find(") const", I)) != std::string::npos;) {
    size_t After = I + 7;
    if (After < Out.size() && isAlnum(Out[After]))
      ++I;
    else
      Out.erase(I + 1, 6);
  }
  return Out;
}

static bool isNameChar(char Ch) {
  return isAlnum(Ch) || Ch == '_' || Ch == '$';
}

// Replaces each renamed mangled name where it stands as its own token inside
// Name, the way __func__._ZL5firstP4Node embeds _ZL5firstP4Node.
static std::string applyRenames(StringRef Name,
                                const StringMap<std::string> &Renames) {
  std::string Out(Name);
  for (const auto &R : Renames) {
    StringRef From = R.getKey();
    for (size_t Pos = Out.find(From); Pos != std::string::npos;
         Pos = Out.find(From, Pos)) {
      size_t End = Pos + From.size();
      bool Bounded = (Pos == 0 || !isNameChar(Out[Pos - 1]) ||
                      Out[Pos - 1] == '$') &&
                     (End == Out.size() || !isNameChar(Out[End]));
      if (!Bounded) {
        Pos = End;
        continue;
      }
      Out.replace(Pos, From.size(), R.getValue());
      Pos += R.getValue().size();
    }
  }
  return Out;
}

template <typename List>
static void collect(List &&L, std::vector<GlobalValue *> &Out) {
  for (auto &G : L)
    Out.push_back(&G);
}

static std::vector<GlobalValue *> globals(Module &M) {
  std::vector<GlobalValue *> Out;
  collect(M.functions(), Out);
  collect(M.globals(), Out);
  collect(M.aliases(), Out);
  collect(M.ifuncs(), Out);
  return Out;
}

// Renames Next's globals and COMDATs to Orig's, or says why they do not pair.
static std::string pairGlobals(Module &Orig, Module &Next) {
  std::vector<GlobalValue *> A = globals(Orig), B = globals(Next);
  if (A.size() != B.size())
    return "global count " + std::to_string(A.size()) + " vs " +
           std::to_string(B.size());
  for (size_t I = 0; I < A.size(); ++I)
    if (A[I]->getValueID() != B[I]->getValueID())
      return "global " + std::to_string(I) + " kind: " +
             A[I]->getName().str() + " vs " + B[I]->getName().str();

  // Mangled names first, so the names that embed them can be checked against
  // the complete rename map.
  StringMap<std::string> Renames;
  for (size_t I = 0; I < A.size(); ++I) {
    StringRef NA = A[I]->getName(), NB = B[I]->getName();
    if (NA == NB || !NA.starts_with("_Z") || !NB.starts_with("_Z"))
      continue;
    if (signature(NA) != signature(NB))
      return "symbol " + std::to_string(I) + ": " + demangle(NA) + " vs " +
             demangle(NB);
    Renames[NB] = NA.str();
  }
  std::vector<std::pair<GlobalValue *, std::string>> Final;
  for (size_t I = 0; I < A.size(); ++I) {
    StringRef NA = A[I]->getName(), NB = B[I]->getName();
    if (NA == NB)
      continue;
    if (!Renames.count(NB) && applyRenames(NB, Renames) != NA)
      return "symbol " + std::to_string(I) + ": " + NA.str() + " vs " +
             NB.str();
    Final.push_back({B[I], NA.str()});
  }
  // Through temporary names, so no rename collides with a name not yet moved.
  for (size_t I = 0; I < Final.size(); ++I)
    Final[I].first->setName("cir.modernize.ircmp." + std::to_string(I));
  for (auto &[GV, Name] : Final) {
    GV->setName(Name);
    if (GV->getName() != Name)
      return "symbol " + Name + " already taken in the rewritten module";
  }

  std::vector<StringRef> Stale;
  for (GlobalObject &GO : Next.global_objects()) {
    Comdat *C = GO.getComdat();
    if (!C)
      continue;
    std::string Name = applyRenames(C->getName(), Renames);
    if (Name == C->getName())
      continue;
    Comdat *N = Next.getOrInsertComdat(Name);
    N->setSelectionKind(C->getSelectionKind());
    GO.setComdat(N);
    Stale.push_back(C->getName());
  }
  for (StringRef Name : Stale) {
    auto &Table = Next.getComdatSymbolTable();
    auto It = Table.find(Name);
    if (It != Table.end() && It->second.getUsers().empty())
      Table.erase(It);
  }
  return "";
}

static void renumberTypes(Module &M) {
  TypeFinder Types;
  Types.run(M, /*onlyNamed=*/true);
  StringMap<unsigned> Count;
  for (StructType *ST : Types) {
    StringRef Name = ST->getName();
    if (!Name.starts_with("struct.") && !Name.starts_with("class.") &&
        !Name.starts_with("union."))
      continue;
    // The first type of a name has no suffix, and which one is first can
    // change too, so unsuffixed names are renumbered with the rest.
    StringRef Base = Name;
    auto [Head, Suffix] = Name.rsplit('.');
    unsigned N;
    if (!Suffix.getAsInteger(10, N))
      Base = Head;
    ST->setName((Base + ".#" + Twine(++Count[Base])).str());
  }
}

// The printer numbers metadata in slot order and keeps the numbers the
// stripped load tags took, so every later node shifts. Nodes are renumbered by
// first use, and their definitions printed in that order.
static std::vector<std::string> renumberMetadata(std::vector<std::string> Lines) {
  DenseMap<unsigned, unsigned> Ids;
  std::vector<unsigned> Order;
  auto Rename = [&](StringRef L) {
    std::string Out;
    for (size_t I = 0; I < L.size(); ++I) {
      bool Ref = L[I] == '!' && I + 1 < L.size() && isDigit(L[I + 1]) &&
                 (I == 0 || (!isNameChar(L[I - 1]) && L[I - 1] != '"'));
      if (!Ref) {
        Out += L[I];
        continue;
      }
      size_t J = I + 1;
      unsigned N = 0;
      while (J < L.size() && isDigit(L[J]))
        N = N * 10 + (L[J++] - '0');
      auto [It, New] = Ids.try_emplace(N, Ids.size());
      if (New)
        Order.push_back(N);
      Out += "!#" + std::to_string(It->second);
      I = J - 1;
    }
    return Out;
  };
  std::vector<std::string> Out;
  DenseMap<unsigned, StringRef> Defs;
  for (const std::string &L : Lines) {
    if (L.size() > 1 && L[0] == '!' && isDigit(L[1])) {
      unsigned N = 0;
      StringRef(L).substr(1).consumeInteger(10, N);
      Defs[N] = L;
    } else {
      Out.push_back(Rename(L));
    }
  }
  // Order grows while this runs: a node can name nodes not numbered yet.
  for (size_t I = 0; I < Order.size(); ++I)
    if (auto It = Defs.find(Order[I]); It != Defs.end())
      Out.push_back(Rename(It->second));
  return Out;
}

static std::vector<std::string> print(Module &M) {
  M.setModuleIdentifier("");
  M.setSourceFileName("");
  std::string S;
  raw_string_ostream OS(S);
  M.print(OS, nullptr);
  std::vector<std::string> Lines;
  for (StringRef Rest = S; !Rest.empty();) {
    auto [Line, Tail] = Rest.split('\n');
    Lines.push_back(Line.str());
    Rest = Tail;
  }
  return renumberMetadata(std::move(Lines));
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv,
                              "prove a cir-modernize rewrite kept the IR\n");
  LLVMContext CA, CB;
  SMDiagnostic Err;
  std::unique_ptr<Module> Orig = parseIRFile(OrigPath, Err, CA);
  if (!Orig) {
    Err.print(argv[0], errs());
    return 2;
  }
  std::unique_ptr<Module> Next = parseIRFile(NextPath, Err, CB);
  if (!Next) {
    Err.print(argv[0], errs());
    return 2;
  }
  stripRefFacts(*Orig);
  stripRefFacts(*Next);
  if (std::string Why = pairGlobals(*Orig, *Next); !Why.empty()) {
    outs() << Why << "\n";
    return 1;
  }
  renumberTypes(*Orig);
  renumberTypes(*Next);

  std::vector<std::string> A = print(*Orig), B = print(*Next);
  if (Print) {
    for (auto *Lines : {&A, &B}) {
      for (const std::string &L : *Lines)
        outs() << L << "\n";
      outs() << "; ----\n";
    }
  }
  std::string Fn;
  for (size_t I = 0; I < std::max(A.size(), B.size()); ++I) {
    StringRef LA = I < A.size() ? StringRef(A[I]) : "<end>";
    StringRef LB = I < B.size() ? StringRef(B[I]) : "<end>";
    if (LA.starts_with("define "))
      Fn = LA.str();
    if (LA != LB) {
      outs() << "in " << (Fn.empty() ? "module" : Fn) << "\n  - " << LA
             << "\n  + " << LB << "\n";
      return 1;
    }
  }
  return 0;
}
