//===- SafeCOwnership.cpp - ownership rules of Safe C, checked on CIR -----===//
//
// The rules and their Lean model: safe-c-notes/core-subset.md and
// safe-c-lean/SafeC/Core*.lean. In short: an owner ('_Owned') is moved by use;
// any other pointer is a borrow with a parent, held only in locals and
// parameters; changing what something owns kills every other borrow with the
// same root except its own ancestors. An operation, call or declaration with no
// rule is an error.
//
// CIR is the subject because it has one 'store' where C has a dozen ways to
// write one. It is made without bounds checks, for this analysis only.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/FrontendAction/SafeCOwnership.h"
#include "mlir/Pass/PassManager.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/Analyses/LifetimeSafety/LifetimeAnnotations.h"
#include "clang/CIR/CIRGenerator.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;

namespace {

StringRef pointerKind(QualType T) { return lifetimes::safeCPointerKind(T); }
bool isOwned(QualType T) { return pointerKind(T) == "owned"; }
bool isTaken(QualType T) { return pointerKind(T).starts_with("taken_if_"); }

/// Every level of a pointer to pointers is an owner.
bool ownedAtEveryLevel(QualType T) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  for (; T->isPointerType(); T = T->getPointeeType())
    if (!T->getPointeeType()->isFunctionType() && !isOwned(T))
      return false;
  return true;
}

//===----------------------------------------------------------------------===//
// Library contracts, by what a call does with pointers. A function whose
// prototype has no pointer needs none. Left out on purpose: strtok, realloc,
// callbacks (qsort, signal), va_list, and whatever keeps a pointer (putenv,
// setvbuf, pthread_create).
//===----------------------------------------------------------------------===//

enum class Lib {
  None,
  Frees,        // takes the owner in the first argument
  Owner,        // returns a new owner
  ZeroedOwner,  // the same, zero-filled
  Reads,        // accesses memory during the call only, returns no pointer
  InsideFirst,  // the same, and the result is null or inside the first argument
  Stops,        // never returns
  Static,       // returns a pointer to storage that lives as long as the program
  UntilNextCall // returns a pointer into storage a later call may change
};

Lib libraryContract(StringRef N) {
  if (N.consume_front("__builtin_"))
    N.consume_front("___");
  N.consume_back("_chk");
  return llvm::StringSwitch<Lib>(N)
      .Cases({"free", "fclose", "pclose", "closedir"}, Lib::Frees)
      .Cases({"malloc", "strdup", "strndup", "fopen", "fdopen", "popen", "opendir"},
             Lib::Owner)
      .Case("calloc", Lib::ZeroedOwner)
      .Cases({"memset", "memcpy", "memmove", "memchr", "strchr", "strrchr",
              "strstr", "strpbrk", "strncpy", "strcpy", "strcat", "strncat",
              "fgets"},
             Lib::InsideFirst)
      .Cases({"abort", "exit", "_exit", "__assert_rtn", "__assert_fail"}, Lib::Stops)
      .Cases({"__error", "__errno_location"}, Lib::Static)
      .Cases({"strerror", "getenv", "localtime", "gmtime", "ctime", "asctime",
              "readdir"},
             Lib::UntilNextCall)
      .Cases({"printf", "fprintf", "dprintf", "snprintf", "sprintf", "puts",
              "fputs", "fputc", "putc", "perror", "fread", "fwrite", "fflush",
              "fseek", "ftell", "rewind", "feof", "ferror", "clearerr", "fileno",
              "fgetc", "getc", "ungetc", "sscanf", "scanf", "fscanf"},
             Lib::Reads)
      .Cases({"read", "write", "pread", "pwrite", "open", "close", "unlink",
              "remove", "rename", "mkdir", "rmdir", "access", "stat", "fstat",
              "lstat", "bind", "connect", "accept", "listen", "setsockopt",
              "getsockopt", "send", "recv", "sendto", "recvfrom", "select",
              "poll", "pipe", "waitpid"},
             Lib::Reads)
      .Cases({"strlen", "strnlen", "strcmp", "strncmp", "strcasecmp",
              "strncasecmp", "memcmp", "strcspn", "strspn", "strlcpy", "strlcat",
              "bzero", "atoi", "atol", "atof", "strtol", "strtoul", "strtoll",
              "strtoull", "strtod", "strtof", "time", "clock_gettime",
              "gettimeofday", "mktime", "strftime", "pthread_mutex_lock",
              "pthread_mutex_unlock", "pthread_mutex_trylock", "sigemptyset",
              "sigfillset", "sigaddset"},
             Lib::Reads)
      .Default(Lib::None);
}

/// The name written in C: macOS renames some symbols ("\01_open$NOCANCEL").
StringRef cName(StringRef N) {
  if (N.consume_front("\01"))
    N.consume_front("_");
  return N.split('$').first;
}

//===----------------------------------------------------------------------===//
// Abstract state
//===----------------------------------------------------------------------===//

using Value = mlir::Value;

struct Node {
  enum Kind { Own, Dead, Static, Maybe, Borrow } K = Dead;
  Value Parent;          // Borrow
  bool Capable = false;  // Borrow: may change what the object owns
  Value Token;           // Maybe: the result that says whether it was taken...
  bool TakenIfNonzero = false; // ...and how
  std::string Why;       // Dead: what ended it
  bool sameAs(const Node &O) const {
    return K == O.K && Parent == O.Parent && Token == O.Token &&
           TakenIfNonzero == O.TakenIfNonzero;
  }
};

/// What is known of a scalar: the truth of a call's result, or a constant.
struct Fact {
  Value Token;
  bool Sense = true;
  std::optional<llvm::APSInt> Constant;
  bool operator==(const Fact &O) const {
    return Token == O.Token && Sense == O.Sense &&
           Constant.has_value() == O.Constant.has_value() &&
           (!Constant || llvm::APSInt::isSameValue(*Constant, *O.Constant));
  }
};

struct State {
  llvm::MapVector<Value, Node> N;
  llvm::DenseMap<Value, Fact> F;
};

/// What an SSA value is, the same on every path.
struct Val {
  enum Kind { Pointer, Address, Local, Null, Owner, OwnerOf, Static, Function } K;
  Value Of; // Pointer, Address: the node; Local, OwnerOf: the variable
  bool Zeroed = false, Typed = false; // Owner
};

struct Rejected {
  mlir::Location Loc;
  std::string Message;
};

//===----------------------------------------------------------------------===//
// Checks on declarations, for a whole file
//===----------------------------------------------------------------------===//

class FileRules {
public:
  ASTContext &Ctx;
  Sema &S;
  FileRules(Sema &S) : Ctx(S.Context), S(S) {}

  bool trusted(const FunctionDecl *FD) const {
    for (const FunctionDecl *R : FD->redecls())
      for (const auto *A : R->specific_attrs<AnnotateAttr>())
        if (A->getAnnotation() == "safec_trusted")
          return true;
    return false;
  }
  bool mine(const Decl *D) const {
    return D->getLocation().isValid() &&
           !Ctx.getSourceManager().isInSystemHeader(D->getLocation());
  }
  bool hasOwners(QualType T) const {
    T = Ctx.getBaseElementType(T).getCanonicalType();
    if (T->isPointerType() || T->isBlockPointerType())
      return true;
    if (const auto *AT = T->getAs<AtomicType>())
      return hasOwners(AT->getValueType());
    const RecordDecl *RD = T->getAsRecordDecl();
    RD = RD ? RD->getDefinition() : nullptr;
    // A library's struct holds the library's pointers.
    return RD && mine(RD) &&
           llvm::any_of(RD->fields(), [&](const FieldDecl *F) {
             return hasOwners(F->getType());
           });
  }
  void error(SourceLocation L, const Twine &Msg) {
    DiagnosticsEngine &D = Ctx.getDiagnostics();
    D.Report(L, D.getCustomDiagID(DiagnosticsEngine::Error, "%0")) << Msg.str();
  }

  void field(const FieldDecl *F) {
    QualType T = F->getType(), E = Ctx.getBaseElementType(T);
    if (!E->isPointerType()) {
      if (F->getParent()->isUnion() && hasOwners(E))
        error(F->getLocation(), "a union holding owners has no checked access; "
                                "use one field per member");
      return;
    }
    if (E->isFunctionPointerType())
      error(F->getLocation(), "a stored function pointer has no ownership rule; "
                              "call the function by name, or mark the function "
                              "that uses this one '_Trusted'");
    else if (F->getParent()->isUnion())
      error(F->getLocation(), "a union holding pointers has no checked access; "
                              "use one field per member");
    else if (!ownedAtEveryLevel(T))
      error(F->getLocation(),
            "'" + F->getNameAsString() +
                "' stores a borrow, which nothing can end when its owner does; "
                "if it frees what it points to declare it '_Owned', otherwise "
                "pass the pointer as a parameter or keep an index");
  }
  void global(const VarDecl *V) {
    if (hasOwners(V->getType()))
      error(V->getLocation(), "'" + V->getNameAsString() +
                                  "' is shared by the whole program and holds a "
                                  "pointer; keep it in a struct that main owns "
                                  "and pass that");
  }
  void function(const FunctionDecl *FD) {
    auto Bad = [&](SourceLocation L, const Twine &M) { error(L, M); };
    QualType R = FD->getReturnType();
    if (R->isPointerType() && R->getPointeeType()->isPointerType())
      Bad(FD->getLocation(), "returns a pointer to a pointer, which has no rule; "
                             "return the inner pointer");
    if (!R->isPointerType() && hasOwners(R))
      Bad(FD->getLocation(), "a struct that owns memory is returned by value, "
                             "which copies its owners; fill one through a pointer");
    for (const ParmVarDecl *P : FD->parameters()) {
      QualType T = P->getType();
      if (T->isFunctionPointerType())
        Bad(P->getLocation(), "a function pointer parameter has no ownership rule");
      else if (T->isPointerType() && T->getPointeeType()->isPointerType() &&
               !FD->isMain())
        Bad(P->getLocation(), "a pointer to a pointer parameter has no rule; "
                              "pass the struct that holds the pointer");
      else if (!T->isPointerType() && hasOwners(T))
        Bad(P->getLocation(), "a struct that owns memory is passed by value, "
                              "which copies its owners; pass a pointer");
    }
    if (const FunctionDecl *Prev = FD->getPreviousDecl())
      for (unsigned I = 0; I < FD->getNumParams() && I < Prev->getNumParams(); ++I)
        if (pointerKind(FD->getParamDecl(I)->getType()) !=
            pointerKind(Prev->getParamDecl(I)->getType()))
          Bad(FD->getParamDecl(I)->getLocation(),
              "an earlier declaration marks this parameter differently");
  }

  struct Local {
    const VarDecl *VD;
    std::string Scope;
  };

  /// Checks a body, and lists its variables with the blocks they live in.
  llvm::StringMap<SmallVector<Local, 1>> body(const FunctionDecl *FD) {
    struct Walk : RecursiveASTVisitor<Walk> {
      FileRules &R;
      llvm::StringMap<SmallVector<Local, 1>> Locals;
      std::string Scope = "/";
      unsigned Next = 0;
      explicit Walk(FileRules &R) : R(R) {}
      bool TraverseStmt(Stmt *St) {
        if (!St || !isa<CompoundStmt, ForStmt, IfStmt, WhileStmt, SwitchStmt,
                        DoStmt, StmtExpr>(St))
          return RecursiveASTVisitor::TraverseStmt(St);
        size_t Size = Scope.size();
        Scope += std::to_string(++Next) + "/";
        bool Ok = RecursiveASTVisitor::TraverseStmt(St);
        Scope.resize(Size);
        return Ok;
      }
      bool VisitVarDecl(VarDecl *V) {
        if (isa<ParmVarDecl>(V))
          return true;
        Locals[V->getName()].push_back({V, Scope});
        QualType T = V->getType(), E = R.Ctx.getBaseElementType(T);
        if (V->isStaticLocal() && R.hasOwners(T))
          R.error(V->getLocation(), "a static local holding a pointer has no rule; "
                                    "pass it as a parameter");
        if (E->isFunctionPointerType())
          R.error(V->getLocation(), "a function pointer variable has no ownership "
                                    "rule; call the function by name");
        if (V->hasAttr<CleanupAttr>())
          R.error(V->getLocation(), "'cleanup' has no ownership rule; free it "
                                    "by name");
        bool Nested = E->isPointerType() && (T->isArrayType() ||
                                             E->getPointeeType()->isPointerType());
        if (Nested && !ownedAtEveryLevel(T))
          R.error(V->getLocation(), "this holds pointers that are not owners, "
                                    "which nothing can end when their owner does");
        return true;
      }
      bool VisitCallExpr(CallExpr *C) {
        const FunctionDecl *F = C->getDirectCallee();
        if (F && (F->getBuiltinID() == Builtin::BI__builtin_unreachable ||
                  F->getBuiltinID() == Builtin::BI__builtin_assume))
          R.error(C->getExprLoc(), "this is believed, not checked; call 'abort()' "
                                   "where the program must not get to");
        return true;
      }
      bool VisitCStyleCastExpr(CStyleCastExpr *C) {
        // Bounds checks contain casts the compiler made up, with no '(' of
        // their own.
        SourceLocation L = R.Ctx.getSourceManager().getSpellingLoc(C->getBeginLoc());
        bool Invalid = false;
        const char *P = R.Ctx.getSourceManager().getCharacterData(L, &Invalid);
        if (C->getType()->isPointerType() && !Invalid && *P == '(' &&
            !C->getSubExpr()->isNullPointerConstant(
                R.Ctx, Expr::NPC_ValueDependentIsNotNull))
          R.error(C->getExprLoc(), "a pointer cast has no ownership rule; declare "
                                   "the pointer with the type it has");
        return true;
      }
    } W(*this);
    W.TraverseStmt(FD->getBody());
    return std::move(W.Locals);
  }
};

//===----------------------------------------------------------------------===//
// One function
//===----------------------------------------------------------------------===//

class FunctionCheck {
  FileRules &R;
  cir::CIRGenerator &Gen;
  cir::FuncOp Fn;
  const FunctionDecl *FD;
  llvm::StringMap<SmallVector<FileRules::Local, 1>> Locals;
  llvm::StringMap<const RecordDecl *> &Records;

  llvm::DenseMap<Value, Val> Vals;
  struct Var {
    const VarDecl *VD = nullptr; // null for the return slot
    std::string Scope, Name;
    bool Pointer = false, Owned = false, Capable = true;
  };
  llvm::DenseMap<Value, Var> Vars;
  llvm::DenseMap<Value, std::string> Globals;
  llvm::DenseSet<Value> Escapes, Temporaries;
  llvm::DenseMap<mlir::Block *, State> In;
  SmallVector<mlir::Block *, 16> Work;
  int FirstBorrow = -1;

  // The block being run.
  State *S = nullptr;
  mlir::Operation *Cur = nullptr;
  SmallVector<Value, 2> Used;


public:
  FunctionCheck(FileRules &R, cir::CIRGenerator &Gen, cir::FuncOp Fn,
                const FunctionDecl *FD,
                llvm::StringMap<const RecordDecl *> &Records)
      : R(R), Gen(Gen), Fn(Fn), FD(FD), Records(Records) {}
  std::optional<Rejected> run();

private:
  std::optional<Rejected> Failure;
  bool Failed() const { return Failure.has_value(); }
  void fail(const Twine &M) {
    if (!Failure)
      Failure = Rejected{Cur ? Cur->getLoc() : Fn.getLoc(), M.str()};
  }

  static mlir::Type pointee(mlir::Type T) {
    return mlir::cast<cir::PointerType>(T).getPointee();
  }
  static bool isPtr(mlir::Type T) { return mlir::isa<cir::PointerType>(T); }
  bool ownersIn(mlir::Type T) const {
    if (isPtr(T))
      return true;
    if (auto A = mlir::dyn_cast<cir::ArrayType>(T))
      return ownersIn(A.getElementType());
    if (auto RT = mlir::dyn_cast<cir::RecordType>(T)) {
      auto It = RT.getName() ? Records.find(RT.getName().getValue()) : Records.end();
      if (It == Records.end())
        return true;
      const RecordDecl *RD = It->second;
      return R.hasOwners(R.Ctx.getCanonicalTagType(RD));
    }
    return false;
  }
  bool libraryRecord(mlir::Type T) const {
    auto RT = mlir::dyn_cast<cir::RecordType>(T);
    if (!RT || !RT.getName())
      return false;
    auto It = Records.find(RT.getName().getValue());
    return It != Records.end() && !R.mine(It->second);
  }

  std::string lineOf(mlir::Operation *Op) const {
    std::string Out;
    Op->getLoc()->walk([&](mlir::Location L) {
      if (auto F = mlir::dyn_cast<mlir::FileLineColLoc>(L)) {
        Out = " on line " + std::to_string(F.getLine());
        return mlir::WalkResult::interrupt();
      }
      return mlir::WalkResult::advance();
    });
    return Out;
  }
  std::string here() const { return lineOf(Cur); }
  std::string nameOf(Value V) const {
    auto It = Vars.find(V);
    return It == Vars.end() || It->second.Name.empty() ? std::string("this pointer")
                                                        : "'" + It->second.Name + "'";
  }

  Node *node(Value V) {
    auto It = S->N.find(V);
    return It == S->N.end() ? nullptr : &It->second;
  }
  static const Node *node(const State &St, Value V) {
    auto It = St.N.find(V);
    return It == St.N.end() ? nullptr : &It->second;
  }
  static bool isAncestor(const State &St, Value A, Value Of) {
    for (unsigned I = 0; Of && I < 1000; ++I) {
      if (Of == A)
        return true;
      const Node *N = node(St, Of);
      Of = N ? N->Parent : Value();
    }
    return false;
  }
  static SmallVector<Value, 8> ancestors(const State &St, Value V) {
    SmallVector<Value, 8> Out;
    const Node *N = node(St, V);
    for (Value P = N ? N->Parent : Value(); P && Out.size() < 1000;) {
      Out.push_back(P);
      N = node(St, P);
      P = N ? N->Parent : Value();
    }
    return Out;
  }
  static Value rootOf(const State &St, Value V) {
    SmallVector<Value, 8> A = ancestors(St, V);
    return A.empty() ? V : A.back();
  }

  Node *live(Value V, const Twine &What) {
    Node *N = node(V);
    if (!N)
      fail(nameOf(V) + " may be unset here");
    else if (N->K == Node::Dead)
      fail(What + " after " + (N->Why.empty() ? std::string("it ended") : N->Why));
    else if (N->K == Node::Maybe)
      fail(What + " before testing whether the call took it; test its result "
                  "first");
    else
      return N;
    return nullptr;
  }
  void kill(Value V, const Twine &Why) {
    Node *N = node(V);
    if (!N || N->K == Node::Static)
      return;
    *N = Node();
    N->Why = Why.str();
  }
  template <class Pred> void killIf(Pred P, const Twine &Why) {
    SmallVector<Value, 8> Out;
    for (auto &KV : S->N)
      if (KV.second.K == Node::Borrow && P(KV.first))
        Out.push_back(KV.first);
    for (Value V : Out)
      kill(V, Why);
  }
  void killDescendants(Value Of, const Twine &Why) {
    killIf([&](Value V) { return V != Of && isAncestor(*S, Of, V); }, Why);
  }
  /// What V's object owns changes: every other borrow with its root ends.
  void mutate(Value V, const Twine &Why) {
    Node *N = live(V, "change");
    if (!N)
      return;
    if (N->K == Node::Static)
      return fail("this points at static data, which owns nothing");
    if (N->K == Node::Borrow && !N->Capable)
      return fail("this pointer is 'const', so what its object owns cannot be "
                  "changed through it; take a pointer that is not 'const'");
    Value Root = rootOf(*S, V);
    SmallVector<Value, 8> Anc = ancestors(*S, V);
    killIf(
        [&](Value X) {
          return X != V && !llvm::is_contained(Anc, X) && rootOf(*S, X) == Root;
        },
        Why);
  }
  /// V is about to mean something else: its borrows now hang off its parent.
  void reparent(Value V) {
    Node *N = node(V);
    Value P = N ? N->Parent : Value();
    for (auto &KV : S->N)
      if (KV.second.Parent == V) {
        if (P) {
          KV.second.Parent = P;
        } else {
          KV.second = Node();
          KV.second.Why = "its source was reassigned";
        }
      }
  }
  void moveOwner(Value V, const Twine &Why) {
    Node *N = live(V, Why);
    if (!N)
      return;
    if (N->K != Node::Own)
      return fail("this is not an owner");
    std::string W = Why.str() + here();
    killDescendants(V, W);
    S->N[V] = Node();
    S->N[V].Why = W;
  }
  void setNode(Value V, Node N) {
    reparent(V);
    S->N[V] = std::move(N);
  }
  Node borrow(Value Parent, bool Capable) {
    Node N;
    N.K = Node::Borrow;
    N.Parent = Parent;
    N.Capable = Capable;
    return N;
  }
  Node plain(Node::Kind K) {
    Node N;
    N.K = K;
    return N;
  }
  bool capable(const Node *N) const {
    return N && (N->K == Node::Own || N->Capable);
  }
  Val *val(Value V) {
    auto It = Vals.find(V);
    return It == Vals.end() ? nullptr : &It->second;
  }
  static Val mk(Val::Kind K, Value Of = {}) {
    Val V;
    V.K = K;
    V.Of = Of;
    return V;
  }

  /// The live node behind a pointer. Nothing for null and static data.
  std::optional<Value> base(Value V, const Twine &What) {
    Val *X = val(V);
    if (!X) {
      fail(What + ": this value has no ownership rule");
      return std::nullopt;
    }
    switch (X->K) {
    case Val::Pointer:
    case Val::Address:
    case Val::OwnerOf: {
      Value N = X->K == Val::OwnerOf ? V : X->Of;
      if (!live(N, What))
        return std::nullopt;
      return N;
    }
    case Val::Local:
      return X->Of;
    case Val::Static:
    case Val::Function:
    case Val::Null:
      return Value();
    case Val::Owner:
      if (!node(V)) {
        fail(What + ": this owner has already moved");
        return std::nullopt;
      }
      if (!live(V, What))
        return std::nullopt;
      return V;
    }
    llvm_unreachable("covered");
  }
  bool isStaticBase(std::optional<Value> B) {
    return !*B || (node(*B) && node(*B)->K == Node::Static);
  }
  void takeOwner(Value V, const Twine &Why) {
    Val *X = val(V);
    if (!X)
      return fail("this value has no ownership rule");
    if (X->K == Val::Null)
      return;
    if (X->K == Val::Owner) {
      Node *N = node(V);
      if (!N || N->K != Node::Own)
        return fail(Why + ": it has already moved");
      return moveOwner(V, Why);
    }
    if (X->K == Val::OwnerOf) {
      Value Of = X->Of;
      if (live(V, Why))
        moveOwner(Of, Why);
      return;
    }
    fail(Why + ": this is a borrow, not an owner; move a field out with "
               "'_Take(...)', or pass on an '_Owned' variable");
  }

  void operation(mlir::Operation *Op);
  void call(cir::CallOp Op);
  void ret(cir::ReturnOp Op);
  void store(cir::StoreOp Op);
  void load(cir::LoadOp Op);
  void cast(cir::CastOp Op);
  void alloca_(cir::AllocaOp Op);
  void flow(mlir::Block *To, mlir::ValueRange Args, State St);
  void block(mlir::Block *B);
  void takenParameters(mlir::Operation *Op, Value Result);
};

void FunctionCheck::flow(mlir::Block *To, mlir::ValueRange Args, State O) {
  if (Failed())
    return;
  for (auto [Arg, V] : llvm::zip(To->getArguments(), Args)) {
    auto FIt = O.F.find(V);
    if (FIt != O.F.end()) {
      Fact F = FIt->second;
      O.F[Arg] = F;
    } else {
      O.F.erase(Arg);
    }
    Val *X = val(V);
    if (!X)
      continue;
    Val NV = mk(Val::Pointer, Arg);
    Node NN;
    const Node *From = nullptr;
    switch (X->K) {
    case Val::OwnerOf:
      NN = borrow(V, true);
      From = node(O, V);
      break;
    case Val::Pointer:
    case Val::Address:
      NV.K = X->K;
      From = node(O, X->Of);
      NN = borrow(X->Of, capable(From));
      break;
    case Val::Null:
    case Val::Static:
      NN = plain(Node::Static);
      break;
    case Val::Owner:
      return fail("an owner flows through '?:', which has no rule; assign it in "
                  "an 'if' and an 'else'");
    case Val::Local:
    case Val::Function:
      return fail("this value flows through '?:', which has no rule");
    }
    if (From && From->K == Node::Static)
      NN = plain(Node::Static);
    if (From && From->K == Node::Dead)
      NN = *From;
    if (Val *Prev = val(Arg)) {
      if (Prev->K == Val::Address || NV.K == Val::Address)
        NV.K = Val::Address;
    }
    Vals[Arg] = NV;
    for (auto &KV : O.N)
      if (KV.second.Parent == Arg) {
        KV.second = Node();
        KV.second.Why = "the loop went round";
      }
    O.N[Arg] = NN;
  }
  auto It = In.find(To);
  if (It == In.end()) {
    In.try_emplace(To, std::move(O));
    Work.push_back(To);
    return;
  }
  State &Old = It->second;
  const State Was = Old;
  bool Changed = false;
  for (auto &KV : Was.N) {
    Value K = KV.first;
    const Node &X = KV.second;
    const Node *Y = node(O, K);
    if (!Y) {
      Old.N.erase(K);
      Changed = true;
      continue;
    }
    Node J;
    if (X.K == Node::Dead || Y->K == Node::Dead) {
      J.Why = X.K == Node::Dead ? X.Why : Y->Why;
    } else if (X.sameAs(*Y)) {
      J = X;
      J.Capable = X.Capable && Y->Capable;
    } else if (X.K == Node::Static && Y->K == Node::Borrow) {
      J = *Y;
    } else if (Y->K == Node::Static && X.K == Node::Borrow) {
      J = X;
    } else if (X.K == Node::Borrow && Y->K == Node::Borrow) {
      SmallVector<Value, 8> AX = ancestors(Was, K), AY = ancestors(O, K);
      auto C = llvm::find_if(AX, [&](Value A) { return llvm::is_contained(AY, A); });
      if (C != AX.end())
        J = borrow(*C, X.Capable && Y->Capable);
      else
        J.Why = "it points to different things on different paths";
    } else {
      J.Why = "it was moved on one path";
    }
    Node &Cur = Old.N[K];
    if (!J.sameAs(Cur) || J.Capable != Cur.Capable) {
      Cur = J;
      Changed = true;
    }
  }
  // A parent only one side knows: the nearest ancestor both know, or none.
  for (auto &KV : Old.N) {
    Node &N = KV.second;
    if (!N.Parent)
      continue;
    if (!node(Old, N.Parent)) {
      const Node *WN = node(Was, KV.first), *ON = node(O, KV.first);
      const State *From = WN && WN->Parent == N.Parent   ? &Was
                          : ON && ON->Parent == N.Parent ? &O
                                                         : nullptr;
      Value Up;
      if (From)
        for (Value A : ancestors(*From, KV.first))
          if (A != KV.first && node(Old, A)) {
            Up = A;
            break;
          }
      Changed = true;
      if (Up) {
        N.Parent = Up;
      } else {
        N = Node();
        N.Why = "its source is gone";
        continue;
      }
    }
    if (const Node *P = node(Old, N.Parent); P && P->K == Node::Dead) {
      std::string Why = P->Why;
      N = Node();
      N.Why = Why;
      Changed = true;
    }
  }
  SmallVector<Value, 8> Drop;
  for (auto &KV : Old.F) {
    auto OIt = O.F.find(KV.first);
    if (OIt == O.F.end() || !(OIt->second == KV.second))
      Drop.push_back(KV.first);
  }
  for (Value V : Drop)
    Old.F.erase(V);
  if (Changed || !Drop.empty())
    Work.push_back(To);
}

void FunctionCheck::alloca_(cir::AllocaOp Op) {
  Value V = Op.getResult();
  mlir::Type T = Op.getAllocaType();
  reparent(V);
  if (Op.isDynamic() && ownersIn(T))
    return fail("a variable-length array of owners has no rule");
  for (auto &KV : S->N)
    if (KV.second.Parent == V) {
      KV.second = Node();
      KV.second.Why = "its variable's scope ended";
    }
  Var X;
  X.Name = Op.getName().str();
  X.Pointer = isPtr(T);
  const ParmVarDecl *Parm = nullptr;
  for (const ParmVarDecl *P : FD->parameters())
    if (!X.Name.empty() && P->getName() == X.Name)
      Parm = P;
  auto LIt = Locals.find(X.Name);
  // The variable this slot is for: by name, and by place when names repeat.
  const FileRules::Local *L = nullptr;
  if (LIt != Locals.end()) {
    unsigned Matches = 0;
    for (const FileRules::Local &C : LIt->second) {
      SourceManager &SM = R.Ctx.getSourceManager();
      bool At = false;
      for (SourceLocation VL : {C.VD->getLocation(), C.VD->getBeginLoc()}) {
        PresumedLoc PL = SM.getPresumedLoc(VL);
        Op.getLoc()->walk([&](mlir::Location Loc) {
          if (auto F = mlir::dyn_cast<mlir::FileLineColLoc>(Loc))
            At |= PL.isValid() && F.getLine() == PL.getLine() &&
                  F.getColumn() == PL.getColumn();
          return mlir::WalkResult::advance();
        });
      }
      if (At) {
        L = &C;
        ++Matches;
      }
    }
    if (Matches != 1) {
      L = LIt->second.size() == 1 && !Parm ? &LIt->second.front() : nullptr;
      if (!L && X.Pointer)
        return fail("cannot tell which '" + X.Name + "' this is; give it its own name");
    }
  }
  if (L) {
    X.VD = L->VD;
    X.Scope = L->Scope;
  } else if (Parm && LIt == Locals.end()) {
    X.VD = Parm;
  }
  if (X.Pointer) {
    if (X.Name == "__retval") {
      X.Owned = isOwned(FD->getReturnType());
      X.Name.clear();
    } else if (X.VD) {
      X.Owned = isOwned(X.VD->getType()) || (Parm && !L && isTaken(X.VD->getType()));
      if (!Parm || L)
        X.Capable = !X.VD->getType()->getPointeeType().isConstQualified();
    } else {
      return fail("a temporary holds a pointer, which has no rule; name it");
    }
    S->N.erase(V);
  } else {
    S->N[V] = plain(Node::Own);
  }
  Vars[V] = X;
  Vals[V] = mk(Val::Local, V);
  S->F.erase(V);
}

void FunctionCheck::cast(cir::CastOp Op) {
  Value Src = Op.getSrc(), Res = Op.getResult();
  mlir::Type From = Src.getType(), To = Res.getType();
  using CK = cir::CastKind;
  CK K = Op.getKind();
  if (!isPtr(From) && !isPtr(To)) {
    auto Bits = [](mlir::Type T) -> unsigned {
      if (mlir::isa<cir::BoolType>(T))
        return 1;
      auto I = mlir::dyn_cast<cir::IntType>(T);
      return I ? I.getWidth() : 0;
    };
    auto It = S->F.find(Src);
    // Only what keeps "zero or not": a narrowing may lose it.
    bool Keeps = K == CK::int_to_bool || K == CK::bool_to_int ||
                 (K == CK::integral && Bits(From) && Bits(To) >= Bits(From));
    if (It == S->F.end() || !Keeps) {
      S->F.erase(Res);
      return;
    }
    Fact F = It->second;
    if (F.Constant && K == CK::int_to_bool)
      F.Constant = llvm::APSInt::get(F.Constant->isZero() ? 0 : 1);
    S->F[Res] = F;
    return;
  }
  if (K == CK::ptr_to_bool) {
    base(Src, "test");
    S->F.erase(Res);
    return;
  }
  if (K == CK::array_to_ptrdecay) {
    Val *V = val(Src);
    if (!V)
      return fail("this value has no ownership rule");
    if (V->K == Val::Local) {
      Value Of = V->Of;
      setNode(Res, borrow(Of, true));
      Vals[Res] = mk(Val::Address, Res);
    } else {
      Val Copy = *V;
      Vals[Res] = Copy;
    }
    return;
  }
  if (K != CK::bitcast)
    return fail("this pointer conversion has no ownership rule");
  Val *P = val(Src);
  if (!P)
    return fail("this value has no ownership rule");
  Val V = *P;
  mlir::Type F = pointee(From), T = pointee(To);
  if (V.K == Val::Null || V.K == Val::Function) {
    Vals[Res] = V;
    return;
  }
  auto Rename = [&](Val N) {
    if (Node *Old = node(Src)) {
      Node Copy = *Old;
      S->N.erase(Src);
      S->N[Res] = Copy;
    }
    Vals[Res] = N;
  };
  bool FromVoid = mlir::isa<cir::VoidType>(F), ToVoid = mlir::isa<cir::VoidType>(T);
  if (V.K == Val::Owner && FromVoid) {
    if (ownersIn(T) && !V.Zeroed)
      return fail("memory that will hold owners must start null; use 'calloc'");
    if (V.Typed)
      return fail("one allocation is given two types");
    Val N = mk(Val::Owner);
    N.Typed = true;
    N.Zeroed = V.Zeroed;
    P->Typed = true;
    return Rename(N);
  }
  auto OnlyFreed = [&] {
    return llvm::all_of(Res.getUsers(), [](mlir::Operation *U) {
      auto C = mlir::dyn_cast<cir::CallOp>(U);
      return C && C.getCallee() && cName(*C.getCallee()) == "free";
    });
  };
  if (ToVoid && OnlyFreed()) {
    if (V.K == Val::Owner)
      return Rename(mk(Val::Owner));
    if (V.K == Val::OwnerOf) {
      setNode(Res, borrow(Src, true));
      Vals[Res] = mk(Val::OwnerOf, V.Of);
      return;
    }
    Vals[Res] = V;
    return;
  }
  if (F == T) {
    if (V.K == Val::Owner)
      return Rename(V);
    if (V.K == Val::OwnerOf) {
      setNode(Res, borrow(Src, true));
      Vals[Res] = mk(Val::OwnerOf, V.Of);
      return;
    }
    Vals[Res] = V;
    return;
  }
  if (!ownersIn(F) && !ownersIn(T) && !mlir::isa<cir::RecordType>(F) &&
      !mlir::isa<cir::RecordType>(T) && V.K != Val::Owner) {
    if (V.K == Val::Local) {
      if (Vars.lookup(V.Of).Pointer)
        return fail("the address of a pointer variable has no rule");
      setNode(Res, borrow(V.Of, true));
      Vals[Res] = mk(Val::Address, Res);
    } else if (V.K == Val::OwnerOf) {
      setNode(Res, borrow(Src, true));
      Vals[Res] = mk(Val::Pointer, Res);
    } else {
      Vals[Res] = V;
    }
    return;
  }
  fail("a pointer conversion between different types has no ownership rule");
}

void FunctionCheck::load(cir::LoadOp Op) {
  Value Addr = Op.getAddr(), Res = Op.getResult();
  mlir::Type T = Res.getType();
  Val *V = val(Addr);
  if (!V)
    return fail("a load from a value with no ownership rule");
  if (V->K == Val::Local) {
    Value A = V->Of;
    const Var &X = Vars[A];
    if (!X.Pointer) {
      if (ownersIn(T))
        return fail("this copies a struct that owns memory; pass a pointer to it");
      auto It = S->F.find(A);
      if (It != S->F.end()) {
        Fact F = It->second;
        S->F[Res] = F;
      } else {
        S->F.erase(Res);
      }
      return;
    }
    reparent(Res);
    Node *N = node(A);
    if (!N)
      return fail(nameOf(A) + " may be unset here");
    if (N->K == Node::Static) {
      S->N[Res] = plain(Node::Static);
      Vals[Res] = mk(Val::Pointer, Res);
      return;
    }
    Node New;
    if (N->K == Node::Dead)
      New = *N;
    else if (N->K == Node::Maybe)
      New.Why = "a call that may have taken it; test that call's result first";
    else
      New = borrow(A, X.Owned ? true : N->Capable);
    S->N[Res] = New;
    Vals[Res] = X.Owned ? mk(Val::OwnerOf, A) : mk(Val::Pointer, Res);
    return;
  }
  std::optional<Value> B = base(Addr, "read");
  if (!B)
    return;
  if (!isPtr(T)) {
    if (ownersIn(T))
      return fail("this copies a struct that owns memory; pass a pointer to it");
    S->F.erase(Res);
    return;
  }
  if (isStaticBase(B)) {
    // stdin, stdout, stderr and main's arguments live as long as the program.
    std::string G = Globals.lookup(Addr);
    bool Chars = mlir::isa<cir::IntType>(pointee(T)) &&
                 mlir::cast<cir::IntType>(pointee(T)).getWidth() == 8;
    if (G == "__stdinp" || G == "__stdoutp" || G == "__stderrp" || G == "stdin" ||
        G == "stdout" || G == "stderr" || (FD->isMain() && Chars)) {
      setNode(Res, plain(Node::Static));
      Vals[Res] = mk(Val::Pointer, Res);
      return;
    }
    return fail("a pointer loaded from static data has no rule");
  }
  if (auto M = Addr.getDefiningOp<cir::GetMemberOp>())
    if (libraryRecord(pointee(M.getAddr().getType())))
      return fail("a pointer in a library's struct has no rule");
  // Read, then null stored to the same place: a move out of it ('_Take').
  bool Take = false;
  for (mlir::Operation *Q = Op->getNextNode(); Q; Q = Q->getNextNode()) {
    if (auto St = mlir::dyn_cast<cir::StoreOp>(Q); St && St.getAddr() == Addr) {
      auto C = St.getValue().getDefiningOp<cir::ConstantOp>();
      Take = C && C.isNullPtr();
      break;
    }
    if (!mlir::isa<cir::ConstantOp>(Q))
      break;
  }
  if (Take) {
    mutate(*B, "'_Take'" + here());
    if (Failed())
      return;
    setNode(Res, plain(Node::Own));
    Val N = mk(Val::Owner);
    N.Typed = true;
    Vals[Res] = N;
    return;
  }
  bool Cap = capable(node(*B));
  setNode(Res, borrow(*B, Cap));
  Vals[Res] = mk(Val::Pointer, Res);
}

void FunctionCheck::store(cir::StoreOp Op) {
  Value V = Op.getValue(), D = Op.getAddr();
  mlir::Type T = V.getType();
  Val *DV = val(D);
  if (!DV)
    return fail("a store to a value with no ownership rule");
  if (DV->K == Val::Local) {
    Value A = DV->Of;
    Var X = Vars[A];
    if (!X.Pointer) {
      if (ownersIn(T)) {
        auto C = V.getDefiningOp<cir::ConstantOp>();
        if (!C || !mlir::isa<cir::ZeroAttr>(C.getValue()))
          return fail("this copies a struct that owns memory; fill it field by "
                      "field");
        killDescendants(A, "it was overwritten" + here());
      }
      auto It = S->F.find(V);
      if (It != S->F.end() && !Escapes.contains(A)) {
        Fact F = It->second;
        S->F[A] = F;
      } else {
        S->F.erase(A);
      }
      return;
    }
    if (auto BA = mlir::dyn_cast<mlir::BlockArgument>(V);
        BA && BA.getOwner()->isEntryBlock()) {
      Node *N = node(V);
      if (!N)
        return fail("this parameter has no ownership rule");
      if (X.Owned) {
        S->N.erase(V);
        S->N[A] = plain(Node::Own);
      } else if (N->K == Node::Static) {
        S->N[A] = plain(Node::Static);
      } else {
        bool Cap = N->Capable;
        S->N[A] = borrow(V, Cap);
      }
      return;
    }
    Val *XV = val(V);
    if (!XV)
      return fail("a pointer value with no ownership rule");
    Val SV = *XV;
    if (X.Owned) {
      takeOwner(V, "moved");
      if (Failed())
        return;
      killDescendants(A, "its owner was given another value" + here());
      S->N[A] = plain(Node::Own);
      return;
    }
    if (SV.K == Val::Null || SV.K == Val::Static) {
      reparent(A);
      S->N[A] = plain(Node::Static);
      return;
    }
    if (SV.K == Val::Function)
      return fail("a function pointer has no ownership rule");
    if (SV.K == Val::Owner)
      return fail("the result is an owner; keep it in an '_Owned' variable");
    std::optional<Value> B = base(V, "copy");
    if (!B)
      return;
    if (isStaticBase(B)) {
      reparent(A);
      S->N[A] = plain(Node::Static);
      return;
    }
    bool Cap = capable(node(*B)) && X.Capable;
    Value P = SV.K == Val::OwnerOf ? SV.Of : *B;
    reparent(A);
    S->N.erase(A);
    // Hang off a named variable, not off a temporary that will be reused.
    while (P) {
      Node *N = node(P);
      bool Temp = !Vars.count(P) && N && N->K == Node::Borrow && N->Parent;
      if (P != A && !Temp)
        break;
      P = N ? N->Parent : Value();
    }
    if (!P)
      return fail("this would make " + nameOf(A) + " borrow from itself");
    // A variable may not be pointed to from outside its block.
    Value Root = rootOf(*S, P);
    auto RIt = Vars.find(Root);
    if (RIt != Vars.end() && !RIt->second.Pointer) {
      const Var &RV = RIt->second;
      if (RV.Scope.empty() || X.Scope.empty())
        return fail(nameOf(A) + " points to a variable or temporary with no known "
                                "block");
      if (!StringRef(X.Scope).starts_with(RV.Scope))
        return fail(nameOf(A) + " outlives " + nameOf(Root) +
                    ", which it would point to; declare " + nameOf(Root) +
                    " in the same block or an outer one");
    }
    S->N[A] = borrow(P, Cap);
    return;
  }
  if (!isPtr(T)) {
    if (!base(D, "write"))
      return;
    if (ownersIn(T))
      return fail("this copies a struct that owns memory; move its fields one by "
                  "one");
    return;
  }
  // The null half of '_Take'.
  Val *XV = val(V);
  if (XV && XV->K == Val::Null)
    for (mlir::Operation *Q = Op->getPrevNode(); Q; Q = Q->getPrevNode()) {
      if (auto L = mlir::dyn_cast<cir::LoadOp>(Q); L && L.getAddr() == D) {
        Val *LV = val(L.getResult());
        if (LV && LV->K == Val::Owner)
          return;
        break;
      }
      if (!mlir::isa<cir::ConstantOp>(Q))
        break;
    }
  takeOwner(V, "stored");
  if (Failed())
    return;
  std::optional<Value> B = base(D, "write");
  if (!B)
    return;
  if (!*B)
    return fail("an owner is stored in static data, which has no rule");
  mutate(*B, "a store" + here());
}

void FunctionCheck::call(cir::CallOp Op) {
  Value Res = Op->getNumResults() ? Op->getResult(0) : Value();
  SmallVector<Value, 4> Expiring;
  for (Value T : Temporaries)
    if (T != Res && node(T))
      Expiring.push_back(T);
  for (Value T : Expiring) {
    bool Here = llvm::any_of(Op.getArgOperands(), [&](Value A) {
      Val *V = val(A);
      return A == T || (V && V->Of && isAncestor(*S, T, V->Of));
    });
    if (!Here) {
      killDescendants(T, "a later call, which may reuse the library's buffer");
      kill(T, "a later call, which may reuse the library's buffer");
    }
  }
  Used.assign(Expiring.begin(), Expiring.end());
  if (Op.isIndirect())
    return fail("a call through a function pointer has no ownership rule; call the "
                "function by name");
  StringRef Sym = *Op.getCallee(), Name = cName(Sym);
  if (Res) {
    for (auto &KV : S->N)
      if (KV.second.K == Node::Maybe && KV.second.Token == Res) {
        KV.second = Node();
        KV.second.Why = "its call's result was tested too late";
      }
    SmallVector<Value, 4> Drop;
    for (auto &KV : S->F)
      if (KV.second.Token == Res)
        Drop.push_back(KV.first);
    for (Value V : Drop)
      S->F.erase(V);
  }
  const auto *Callee =
      dyn_cast_or_null<FunctionDecl>(Gen.getDeclForMangledName(Sym));
  mlir::OperandRange Args = Op.getArgOperands();
  bool ResPtr = Res && isPtr(Res.getType());
  for (Value A : Args) {
    if (!isPtr(A.getType()))
      continue;
    Val *V = val(A);
    if (!V)
      return fail("this argument has no ownership rule");
    if (V->K == Val::Local && Vars.lookup(V->Of).Pointer)
      return fail("the address of a pointer variable is passed, which has no rule; "
                  "pass the struct that holds the pointer, or return the pointer");
    if (V->K == Val::Function)
      return fail("a function passed as an argument has no ownership rule");
  }
  auto NewOwner = [&](bool Zeroed, bool Typed) {
    setNode(Res, plain(Node::Own));
    Val N = mk(Val::Owner);
    N.Zeroed = Zeroed;
    N.Typed = Typed;
    Vals[Res] = N;
  };
  const bool Ours = Callee && R.mine(Callee) && !Callee->getBuiltinID();
  if (!Ours) {
    Lib K = libraryContract(Name);
    if (K == Lib::None && !ResPtr && Callee && !Callee->isVariadic() &&
        llvm::none_of(Callee->parameters(), [](const ParmVarDecl *P) {
          return P->getType()->isAnyPointerType();
        }) &&
        llvm::none_of(Args, [](Value A) { return isPtr(A.getType()); }))
      K = Lib::Reads;
    if (K == Lib::None)
      return fail("'" + Name + "' has no ownership contract; call it from a small "
                               "function marked '_Trusted' whose parameters say "
                               "what it frees");
    if (K == Lib::Static) {
      Vals[Res] = mk(Val::Static);
      return;
    }
    if (K == Lib::Frees) {
      takeOwner(Args[0], Name == "free" ? "freed" : "closed");
      if (Res)
        S->F.erase(Res);
      return;
    }
    for (Value A : Args) {
      if (!isPtr(A.getType()))
        continue;
      if (!base(A, "passed to '" + Name + "'"))
        return;
      if (ownersIn(pointee(A.getType())) && !libraryRecord(pointee(A.getType())))
        return fail("'" + Name + "' works on bytes, and these hold owners");
      if (val(A)->K == Val::Owner)
        return fail("this owner would be lost");
    }
    switch (K) {
    case Lib::Owner:
      return NewOwner(false, Name != "malloc");
    case Lib::ZeroedOwner:
      return NewOwner(true, false);
    case Lib::UntilNextCall:
      setNode(Res, borrow(Value(), false));
      Temporaries.insert(Res);
      Vals[Res] = mk(Val::Pointer, Res);
      return;
    case Lib::InsideFirst: {
      std::optional<Value> B = base(Args[0], "passed");
      if (!B || !Res)
        return;
      if (!*B) {
        Vals[Res] = mk(Val::Static);
        return;
      }
      setNode(Res, borrow(*B, false));
      Vals[Res] = mk(Val::Pointer, Res);
      return;
    }
    default:
      if (ResPtr)
        return fail("the result of '" + Name + "' has no rule");
      if (Res)
        S->F.erase(Res);
      return;
    }
  }
  if (Args.size() > Callee->getNumParams())
    return fail("a variadic function has no ownership rule");
  struct Use {
    Value Base;
    bool Changes;
    unsigned Index;
  };
  SmallVector<Use, 4> Uses;
  for (unsigned I = 0; I < Args.size(); ++I) {
    QualType PT = Callee->getParamDecl(I)->getType();
    if (!PT->isPointerType() || isOwned(PT) || isTaken(PT))
      continue;
    std::optional<Value> B = base(Args[I], "passed to '" + Name + "'");
    if (!B)
      return;
    bool Owners = ownersIn(pointee(Args[I].getType()));
    bool Const = PT->getPointeeType().isConstQualified();
    if (isStaticBase(B)) {
      if (!Const && Owners && val(Args[I])->K == Val::Static)
        return fail("static data is passed to '" + Name + "', which may change it");
      continue;
    }
    Uses.push_back({*B, !Const && Owners, I});
  }
  for (const Use &U : Uses)
    for (const Use &W : Uses)
      if (&U != &W && U.Changes && rootOf(*S, U.Base) == rootOf(*S, W.Base))
        return fail("two arguments of '" + Name + "' reach the same owner and it "
                    "may change one; make that parameter 'const', or pass a copy");
  // A result that is a borrow comes from the first borrowed parameter.
  int First = -1;
  for (unsigned I = 0; I < Callee->getNumParams() && First < 0; ++I) {
    QualType PT = Callee->getParamDecl(I)->getType();
    if (PT->isPointerType() && !isOwned(PT) && !isTaken(PT))
      First = I;
  }
  const Use *From = nullptr;
  for (const Use &U : Uses)
    if (int(U.Index) == First)
      From = &U;
  bool FromCap = From && capable(node(From->Base));
  SmallVector<std::pair<Value, bool>, 2> Pending;
  for (unsigned I = 0; I < Args.size(); ++I) {
    QualType PT = Callee->getParamDecl(I)->getType();
    if (!PT->isPointerType() || !(isOwned(PT) || isTaken(PT)))
      continue;
    if (!isTaken(PT)) {
      takeOwner(Args[I], "passed to '" + Name + "'");
      if (Failed())
        return;
      continue;
    }
    Val *V = val(Args[I]);
    if (V->K == Val::Null)
      continue;
    if (V->K != Val::OwnerOf)
      return fail("an owner that may be given back must be a variable");
    if (!live(Args[I], "passed"))
      return;
    Value A = V->Of;
    Node *N = node(A);
    if (!N || N->K != Node::Own)
      return fail("this is not an owner");
    killDescendants(A, "it was passed to '" + Name + "'");
    Pending.push_back({A, pointerKind(PT) == "taken_if_true"});
    S->N[A] = Node();
    S->N[A].Why = "it was passed twice";
  }
  for (const Use &U : Uses)
    if (!live(U.Base, "passed to '" + Name + "'"))
      return;
  for (const Use &U : Uses)
    if (U.Changes) {
      mutate(U.Base, "the call to '" + Name + "'" + here());
      if (Failed())
        return;
    }
  for (auto &[A, IfTrue] : Pending) {
    if (!Res || ResPtr)
      return fail("'" + Name + "' takes an owner on success but its result does "
                               "not say whether it did");
    Node N = plain(Node::Maybe);
    N.Token = Res;
    N.TakenIfNonzero = IfTrue;
    S->N[A] = N;
  }
  if (!Res)
    return;
  if (!ResPtr) {
    Fact F;
    F.Token = Res;
    S->F[Res] = F;
    if (ownersIn(Res.getType()))
      fail("a struct that owns memory is returned by value");
    return;
  }
  if (isOwned(Callee->getReturnType()))
    return NewOwner(true, true);
  if (!From) {
    if (First < 0)
      return fail("'" + Name + "' returns a borrow of nothing; if the caller frees "
                               "the result, mark it '_Owned'");
    setNode(Res, plain(Node::Static));
    Vals[Res] = mk(Val::Pointer, Res);
    return;
  }
  if (!live(From->Base, "the result"))
    return;
  setNode(Res, borrow(From->Base, FromCap));
  Vals[Res] = mk(Val::Pointer, Res);
}

void FunctionCheck::takenParameters(mlir::Operation *Op, Value Result) {
  for (const ParmVarDecl *P : FD->parameters()) {
    if (!isTaken(P->getType()))
      continue;
    Value A;
    for (auto &KV : Vars)
      if (KV.second.VD == P)
        A = KV.first;
    const Node *N = A ? node(A) : nullptr;
    auto It = Result ? S->F.find(Result) : S->F.end();
    if (!Result || It == S->F.end() || !It->second.Constant)
      return fail("this returns a value that does not say whether '" +
                  P->getNameAsString() + "' was taken; return a constant on each "
                  "path");
    bool Success = pointerKind(P->getType()) == "taken_if_true"
                       ? !It->second.Constant->isZero()
                       : It->second.Constant->isZero();
    bool Held = N && N->K == Node::Own;
    if (Success == Held)
      return fail(Success ? "this reports success without taking '" +
                                P->getNameAsString() + "'"
                          : "this reports failure after taking '" +
                                P->getNameAsString() + "'");
  }
}

void FunctionCheck::ret(cir::ReturnOp Op) {
  Value V = Op->getNumOperands() ? Op->getOperand(0) : Value();
  if (V && isPtr(V.getType())) {
    Val *X = val(V);
    if (!X)
      return fail("the returned value has no ownership rule");
    if (isOwned(FD->getReturnType())) {
      takeOwner(V, "returned");
    } else if (X->K != Val::Null && X->K != Val::Static) {
      std::optional<Value> B = base(V, "returned");
      if (!B)
        return;
      if (!isStaticBase(B)) {
        Value Root = rootOf(*S, *B);
        if (FirstBorrow < 0 || Root != Fn.getArgument(FirstBorrow))
          return fail("a borrow may only be returned from inside the first "
                      "parameter that is not '_Owned'; if the caller frees it, "
                      "mark the result '_Owned'");
      }
    }
    if (Failed())
      return;
  }
  takenParameters(Op, V);
}

void FunctionCheck::operation(mlir::Operation *Op) {
  if (auto A = mlir::dyn_cast<cir::AllocaOp>(Op))
    return alloca_(A);
  if (auto C = mlir::dyn_cast<cir::CastOp>(Op))
    return cast(C);
  if (auto L = mlir::dyn_cast<cir::LoadOp>(Op))
    return load(L);
  if (auto St = mlir::dyn_cast<cir::StoreOp>(Op))
    return store(St);
  if (auto C = mlir::dyn_cast<cir::CallOp>(Op))
    return call(C);
  if (auto Rt = mlir::dyn_cast<cir::ReturnOp>(Op))
    return ret(Rt);
  if (auto C = mlir::dyn_cast<cir::ConstantOp>(Op)) {
    Value Res = C.getResult();
    if (isPtr(Res.getType())) {
      if (!C.isNullPtr())
        return fail("a constant pointer has no ownership rule");
      Vals[Res] = mk(Val::Null);
      return;
    }
    if (mlir::isa<cir::RecordType, cir::ArrayType>(Res.getType()) &&
        ownersIn(Res.getType()) && !mlir::isa<cir::ZeroAttr>(C.getValue()))
      return fail("a constant holding pointers has no ownership rule");
    Fact F;
    if (auto I = mlir::dyn_cast<cir::IntAttr>(C.getValue()))
      F.Constant = llvm::APSInt(I.getValue(), /*isUnsigned=*/true);
    else if (auto B = mlir::dyn_cast<cir::BoolAttr>(C.getValue()))
      F.Constant = llvm::APSInt::get(B.getValue() ? 1 : 0);
    if (F.Constant)
      S->F[Res] = F;
    return;
  }
  if (auto G = mlir::dyn_cast<cir::GetGlobalOp>(Op)) {
    Value Res = G->getResult(0);
    StringRef Name = cName(G.getName());
    Globals[Res] = Name.str();
    if (mlir::isa<cir::FuncType>(pointee(Res.getType()))) {
      Vals[Res] = mk(Val::Function);
      return;
    }
    Vals[Res] = mk(Val::Static);
    const auto *D = dyn_cast_or_null<VarDecl>(Gen.getDeclForMangledName(G.getName()));
    if (D && R.mine(D) && ownersIn(pointee(Res.getType())))
      fail("this global holds pointers");
    return;
  }
  if (mlir::isa<cir::GetMemberOp, cir::PtrStrideOp, cir::GetElementOp>(Op)) {
    Value Src = Op->getOperand(0), Res = Op->getResult(0);
    Val *V = val(Src);
    if (!V)
      return fail("this value has no ownership rule");
    if (V->K == Val::Local) {
      if (Vars.lookup(V->Of).Pointer)
        return fail("the address of a pointer variable has no rule");
      Value Of = V->Of;
      setNode(Res, borrow(Of, true));
      Vals[Res] = mk(Val::Address, Res);
      return;
    }
    if (V->K == Val::Static) {
      Vals[Res] = mk(Val::Static);
      return;
    }
    std::optional<Value> B = base(Src, "use");
    if (!B)
      return;
    if (isStaticBase(B)) {
      Vals[Res] = mk(Val::Static);
      // main's arguments keep their kind.
      return;
    }
    bool Cap = capable(node(*B));
    setNode(Res, borrow(*B, Cap));
    Vals[Res] = mk(Val::Address, Res);
    return;
  }
  if (auto B = mlir::dyn_cast<cir::BrOp>(Op))
    return flow(B.getDest(), B.getDestOperands(), *S);
  if (auto B = mlir::dyn_cast<cir::BrCondOp>(Op)) {
    auto It = S->F.find(B.getCond());
    std::optional<Fact> F;
    if (It != S->F.end())
      F = It->second;
    for (bool Taken : {true, false}) {
      State St = *S;
      if (F && F->Token)
        for (auto &KV : St.N)
          if (KV.second.K == Node::Maybe && KV.second.Token == F->Token) {
            bool Nonzero = Taken == F->Sense;
            bool Took = Nonzero == KV.second.TakenIfNonzero;
            KV.second = Node();
            if (Took)
              KV.second.Why = "the call took it";
            else
              KV.second.K = Node::Own;
          }
      flow(Taken ? B.getDestTrue() : B.getDestFalse(),
           Taken ? B.getDestOperandsTrue() : B.getDestOperandsFalse(),
           std::move(St));
    }
    return;
  }
  if (auto Sw = mlir::dyn_cast<cir::SwitchFlatOp>(Op)) {
    if (Sw->getNumOperands() > 1)
      return fail("this 'switch' has no ownership rule");
    for (mlir::Block *B : Sw->getSuccessors())
      flow(B, {}, *S);
    return;
  }
  if (auto C = mlir::dyn_cast<cir::CmpOp>(Op)) {
    Value L = C.getLhs(), Rh = C.getRhs(), Res = C.getResult();
    if (isPtr(L.getType())) {
      if (base(L, "compared"))
        base(Rh, "compared");
      S->F.erase(Res);
      return;
    }
    auto X = S->F.find(L), Y = S->F.find(Rh);
    bool Eq = C.getKind() == cir::CmpOpKind::eq, Ne = C.getKind() == cir::CmpOpKind::ne;
    if ((Eq || Ne) && X != S->F.end() && X->second.Token && Y != S->F.end() &&
        Y->second.Constant && Y->second.Constant->isZero()) {
      Fact F = X->second;
      if (Eq)
        F.Sense = !F.Sense;
      S->F[Res] = F;
    } else {
      S->F.erase(Res);
    }
    return;
  }
  if (mlir::isa<cir::NotOp>(Op)) {
    Value Res = Op->getResult(0);
    auto X = S->F.find(Op->getOperand(0));
    if (X != S->F.end() && X->second.Token && mlir::isa<cir::BoolType>(Res.getType())) {
      Fact F = X->second;
      F.Sense = !F.Sense;
      S->F[Res] = F;
    } else {
      S->F.erase(Res);
    }
    return;
  }
  StringRef N = Op->getName().stripDialect();
  if (N == "trap" || N == "unreachable")
    return;
  if (N == "copy" || N.starts_with("libc.mem")) {
    for (unsigned I = 0; I < 2 && I < Op->getNumOperands(); ++I) {
      Value A = Op->getOperand(I);
      if (!isPtr(A.getType()))
        continue;
      Val *V = val(A);
      if (ownersIn(pointee(A.getType())))
        return fail("this copies bytes that hold owners; move them one by one");
      if (V && V->K == Val::Local && Vars.lookup(V->Of).Pointer)
        return fail("the address of a pointer variable has no rule");
      if (!base(A, "copied"))
        return;
    }
    if (Op->getNumResults()) {
      Val *V = val(Op->getOperand(0));
      if (V) {
        Val Copy = *V;
        Vals[Op->getResult(0)] = Copy;
      }
    }
    return;
  }
  if (N == "ptr_diff") {
    if (base(Op->getOperand(0), "subtracted"))
      base(Op->getOperand(1), "subtracted");
    return;
  }
  if (N == "var_annotation" || N.starts_with("lifetime.start"))
    return;
  // No pointer goes in or out, so it can neither free nor keep one.
  auto Holds = [&](mlir::Type T) { return isPtr(T) || mlir::isa<cir::RecordType>(T); };
  if (Op->getNumSuccessors() || Op->getNumRegions() ||
      llvm::any_of(Op->getOperandTypes(), Holds) ||
      llvm::any_of(Op->getResultTypes(), Holds) || N.contains("asm") ||
      N.contains("call") || N.contains("throw"))
    return fail("the operation '" + N + "' has no ownership rule");
  for (Value Res : Op->getResults())
    S->F.erase(Res);
}

void FunctionCheck::block(mlir::Block *B) {
  State St = In[B];
  S = &St;
  for (mlir::Operation &Op : *B) {
    Cur = &Op;
    if (!Used.empty() && !mlir::isa_and_nonnull<cir::CallOp>(&Op)) {
      for (Value T : Used) {
        const char *Why = "the call that used it, which may have reused the "
                          "library's buffer";
        killDescendants(T, Why);
        kill(T, Why);
      }
      Used.clear();
    } else if (!Used.empty()) {
      SmallVector<Value, 2> Last = std::move(Used);
      Used.clear();
      for (Value T : Last) {
        const char *Why = "the call that used it, which may have reused the "
                          "library's buffer";
        killDescendants(T, Why);
        kill(T, Why);
      }
    }
    operation(&Op);
    if (Failed())
      return;
    if (auto C = mlir::dyn_cast<cir::CallOp>(&Op);
        C && C.getCallee() && libraryContract(cName(*C.getCallee())) == Lib::Stops) {
      const auto *D = dyn_cast_or_null<FunctionDecl>(
          Gen.getDeclForMangledName(*C.getCallee()));
      if (!D || !R.mine(D) || D->getBuiltinID()) {
        Used.clear();
        return; // never comes back
      }
    }
  }
}

std::optional<Rejected> FunctionCheck::run() {
  Locals = R.body(FD);
  mlir::Block &Entry = Fn.getBody().front();
  State St;
  for (unsigned I = 0; I < FD->getNumParams(); ++I) {
    QualType T = FD->getParamDecl(I)->getType();
    if (!T->isPointerType())
      continue;
    if (I >= Entry.getNumArguments()) {
      fail("this function's parameters have no ownership rule");
      return Failure;
    }
    Value A = Entry.getArgument(I);
    if (isOwned(T) || isTaken(T)) {
      St.N[A] = plain(Node::Own);
    } else {
      if (FirstBorrow < 0)
        FirstBorrow = I;
      St.N[A] = FD->isMain()
                    ? plain(Node::Static)
                    : borrow(Value(), !T->getPointeeType().isConstQualified());
    }
    Vals[A] = mk(Val::Pointer, A);
  }
  // A scalar is followed only if nothing but its own loads and stores has its
  // address; a parameter that may be given back is never assigned.
  llvm::DenseMap<StringRef, unsigned> Stores;
  Fn.walk([&](mlir::Operation *Op) {
    for (mlir::OpOperand &O : Op->getOpOperands()) {
      auto A = O.get().getDefiningOp<cir::AllocaOp>();
      if (!A)
        continue;
      if (auto Sto = mlir::dyn_cast<cir::StoreOp>(Op); Sto && Sto.getAddr() == O.get()) {
        ++Stores[A.getName()];
        continue;
      }
      if (!mlir::isa<cir::LoadOp>(Op))
        Escapes.insert(O.get());
    }
  });
  for (const ParmVarDecl *P : FD->parameters())
    if (isTaken(P->getType()) && Stores.lookup(P->getName()) > 1) {
      fail("'" + P->getNameAsString() + "' may have to be given back, so it cannot "
                                        "be assigned; use another variable");
      return Failure;
    }
  In.try_emplace(&Entry, std::move(St));
  Work.push_back(&Entry);
  for (unsigned Steps = 0; !Work.empty(); ++Steps) {
    if (Steps > 20000) {
      fail("this function is too tangled to check; split it");
      break;
    }
    block(Work.pop_back_val());
    if (Failed())
      break;
  }
  return Failure;
}

//===----------------------------------------------------------------------===//
// The whole file
//===----------------------------------------------------------------------===//

class Consumer : public ASTConsumer {
  CompilerInstance &CI;
  SmallVector<DeclGroupRef, 64> Groups;
  SmallVector<VarDecl *, 8> Tentative;

public:
  explicit Consumer(CompilerInstance &CI) : CI(CI) {}
  bool HandleTopLevelDecl(DeclGroupRef D) override {
    Groups.push_back(D);
    return true;
  }
  void CompleteTentativeDefinition(VarDecl *D) override { Tentative.push_back(D); }
  void HandleTranslationUnit(ASTContext &Ctx) override;
};

SourceLocation sourceOf(ASTContext &Ctx, mlir::Location L) {
  SourceManager &SM = Ctx.getSourceManager();
  SourceLocation Out;
  L->walk([&](mlir::Location X) {
    auto F = mlir::dyn_cast<mlir::FileLineColLoc>(X);
    if (!F)
      return mlir::WalkResult::advance();
    if (auto File = SM.getFileManager().getOptionalFileRef(F.getFilename().getValue()))
      Out = SM.translateFileLineCol(&File->getFileEntry(), F.getLine(),
                                    F.getColumn());
    return mlir::WalkResult::interrupt();
  });
  return Out;
}

void Consumer::HandleTranslationUnit(ASTContext &Ctx) {
  DiagnosticsEngine &Diags = CI.getDiagnostics();
  if (Diags.hasErrorOccurred())
    return;
  FileRules R(CI.getSema());
  llvm::StringMap<const RecordDecl *> Records;
  SmallVector<const FunctionDecl *, 32> Functions;
  struct Collect : RecursiveASTVisitor<Collect> {
    llvm::StringMap<const RecordDecl *> &Records;
    explicit Collect(llvm::StringMap<const RecordDecl *> &M) : Records(M) {}
    bool VisitRecordDecl(RecordDecl *RD) {
      if (!RD->isThisDeclarationADefinition())
        return true;
      StringRef N = RD->getName();
      if (N.empty() && RD->getTypedefNameForAnonDecl())
        N = RD->getTypedefNameForAnonDecl()->getName();
      if (!N.empty())
        Records[N] = RD;
      return true;
    }
  } C(Records);
  C.TraverseDecl(Ctx.getTranslationUnitDecl());

  for (DeclGroupRef G : Groups)
    for (Decl *D : G) {
      if (!R.mine(D))
        continue;
      if (auto *FD = dyn_cast<FunctionDecl>(D)) {
        if (!FD->isImplicit())
          R.function(FD);
        if (FD->isThisDeclarationADefinition() && !R.trusted(FD))
          Functions.push_back(FD);
      } else if (auto *VD = dyn_cast<VarDecl>(D)) {
        R.global(VD);
      }
    }
  for (auto &KV : Records)
    if (R.mine(KV.second))
      for (const FieldDecl *F : KV.second->fields())
        R.field(F);
  if (Diags.hasErrorOccurred())
    return;

  // Unoptimized, whatever the program is compiled with: lifetime markers merge
  // the paths that leave a block.
  CodeGenOptions Opts = CI.getCodeGenOpts();
  Opts.OptimizationLevel = 0;
  Opts.DisableLifetimeMarkers = true;
  cir::CIRGenerator Gen(Diags, &CI.getVirtualFileSystem(), Opts);
  Gen.Initialize(Ctx);
  for (DeclGroupRef G : Groups)
    Gen.HandleTopLevelDecl(G);
  for (VarDecl *VD : Tentative)
    Gen.CompleteTentativeDefinition(VD);
  // Nothing calls an unused static function, but it is checked all the same.
  Gen.emitEvenIfUnused(Functions);
  Gen.HandleTranslationUnit(Ctx);
  if (Diags.hasErrorOccurred() || !Gen.getModule())
    return;
  mlir::PassManager PM(&Gen.getMLIRContext());
  PM.addPass(mlir::createCIRFlattenCFGPass());
  PM.addPass(mlir::createGotoSolverPass());
  if (mlir::failed(PM.run(Gen.getModule()))) {
    R.error(SourceLocation(), "this file's ownership could not be checked");
    return;
  }
  llvm::DenseSet<const FunctionDecl *> Seen;
  SmallVector<cir::FuncOp, 32> Fns;
  Gen.getModule().walk([&](cir::FuncOp F) { Fns.push_back(F); });
  for (cir::FuncOp F : Fns) {
    if (F.isDeclaration())
      continue;
    const auto *FD =
        dyn_cast_or_null<FunctionDecl>(Gen.getDeclForMangledName(F.getSymName()));
    const FunctionDecl *Def = FD ? FD->getDefinition() : nullptr;
    if (!Def || !llvm::is_contained(Functions, Def))
      continue;
    Seen.insert(Def);
    FunctionCheck Check(R, Gen, F, Def, Records);
    if (std::optional<Rejected> Bad = Check.run()) {
      SourceLocation L = sourceOf(Ctx, Bad->Loc);
      R.error(L.isValid() ? L : Def->getLocation(), Bad->Message);
    }
  }
  for (const FunctionDecl *FD : Functions)
    if (!Seen.contains(FD))
      R.error(FD->getLocation(), "'" + FD->getNameAsString() +
                                     "' was not checked: it has no CIR");
}

class Action : public WrapperFrontendAction {
public:
  explicit Action(std::unique_ptr<FrontendAction> Inner)
      : WrapperFrontendAction(std::move(Inner)) {}

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef File) override {
    std::unique_ptr<ASTConsumer> Inner =
        WrapperFrontendAction::CreateASTConsumer(CI, File);
    if (!Inner)
      return nullptr;
    std::vector<std::unique_ptr<ASTConsumer>> Both;
    Both.push_back(std::make_unique<Consumer>(CI));
    Both.push_back(std::move(Inner));
    return std::make_unique<MultiplexConsumer>(std::move(Both));
  }
};

} // namespace

std::unique_ptr<FrontendAction>
cir::wrapWithSafeCOwnership(std::unique_ptr<FrontendAction> Inner) {
  return std::make_unique<Action>(std::move(Inner));
}
