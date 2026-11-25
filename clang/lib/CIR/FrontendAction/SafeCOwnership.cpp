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
#include "clang/AST/SafeCTypes.h"
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
#include <map>

using namespace clang;

namespace {

StringRef pointerKind(QualType T) { return safec::pointerKind(T); }
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
// prototype has no pointer needs none. Left out on purpose: strtok, va_list, and
// whatever keeps a pointer (putenv, setvbuf, pthread_create).
//===----------------------------------------------------------------------===//

enum class Lib {
  None,
  Frees,        // takes the owner in the first argument
  Owner,        // returns a new owner
  ZeroedOwner,  // the same, zero-filled
  Reads,        // accesses memory during the call only, returns no pointer
  InsideFirst,  // the same, and the result is null or inside the first argument
  InsideSecond, // the same, the second
  Callback,     // reads, and calls the function it is given during the call or,
                // when that takes no pointer, at any time
  EndOut,       // reads, and sets *arg 2 to null or inside the first argument
  OwnerOut,     // reads, and sets *arg 1 to a new owner, or null, or not at all
  Reallocs,     // takes the owner in the first argument, returns a new one
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
      .Case("realloc", Lib::Reallocs)
      .Cases({"qsort", "signal", "atexit"}, Lib::Callback)
      .Case("bsearch", Lib::InsideSecond)
      .Cases({"strtol", "strtoul", "strtoll", "strtoull", "strtod", "strtof"},
             Lib::EndOut)
      .Case("asprintf", Lib::OwnerOut)
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
              "bzero", "atoi", "atol", "atof", "time", "clock_gettime",
              "setenv", "unsetenv", "gettimeofday", "mktime", "strftime", "pthread_mutex_lock",
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
  // Raw: an '_Unchecked' pointer, which is compared and copied but not followed.
  enum Kind { Pointer, Address, Local, Null, Owner, OwnerOf, Static, Function, Raw,
              View, ViewAt } K; // a '_View' struct by value, and
              // behind a parameter that may change it
  Value Of; // Pointer, Address: the node; Local, OwnerOf: the variable
  bool Zeroed = false, Typed = false; // Owner
  bool Table = false; // Static: inside a constant table of static pointers
};

struct Rejected {
  mlir::Location Loc;
  std::string Message;
  SmallVector<mlir::Location, 2> Around = {};
};

//===----------------------------------------------------------------------===//
// Checks on declarations, for a whole file
//===----------------------------------------------------------------------===//

class FileRules {
public:
  ASTContext &Ctx;
  Sema &S;
  std::string ResourceDir; // safec.h and the compiler's other headers
  FileRules(Sema &S, StringRef ResourceDir)
      : Ctx(S.Context), S(S), ResourceDir(ResourceDir) {}

  bool trusted(const FunctionDecl *FD) const {
    for (const FunctionDecl *R : FD->redecls())
      for (const auto *A : R->specific_attrs<AnnotateAttr>())
        if (A->getAnnotation() == "safec_trusted")
          return true;
    return false;
  }
  bool mine(const Decl *D) const {
    SourceManager &SM = Ctx.getSourceManager();
    if (D->getLocation().isInvalid() || SM.isInSystemHeader(D->getLocation()))
      return false;
    return ResourceDir.empty() ||
           !SM.getFilename(SM.getExpansionLoc(D->getLocation()))
                .starts_with(ResourceDir);
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

  /// A '_View' struct holds borrows, each as if it were a variable of its own:
  /// it lives in locals and parameters.
  const RecordDecl *view(QualType T) const {
    const RecordDecl *RD = T->getAsRecordDecl();
    RD = RD ? RD->getDefinition() : nullptr;
    if (RD && mine(RD))
      for (const auto *A : RD->specific_attrs<AnnotateAttr>())
        if (A->getAnnotation() == "safec_view")
          return RD;
    return nullptr;
  }
  static unsigned pointersIn(const RecordDecl *RD) {
    return llvm::count_if(RD->fields(), [](const FieldDecl *F) {
      return F->getType()->isPointerType() && !F->getType()->isFunctionPointerType();
    });
  }
  /// A view reached through a pointer, or in an array.
  bool indirectView(QualType T) const {
    bool Through = false;
    for (;; Through = true) {
      if (T->isPointerType())
        T = T->getPointeeType();
      else if (const ArrayType *AT = T->getAsArrayTypeUnsafe())
        T = AT->getElementType();
      else
        return Through && view(T);
    }
  }
  /// Changing a view with one pointer is 'x = f(x)': the pointer it leaves with
  /// is inside the one it came with.
  bool changesView(QualType T) const {
    return T->isPointerType() && !T->getPointeeType().isConstQualified() &&
           view(T->getPointeeType());
  }
  bool constViewPointer(QualType T) const {
    return T->isPointerType() && T->getPointeeType().isConstQualified() &&
           view(T->getPointeeType());
  }

  /// Constant at every level, so its pointers are the ones it was initialized
  /// with, and those are addresses of globals and literals.
  bool isTable(QualType T) const {
    QualType E = Ctx.getBaseElementType(T);
    if (!E.isConstQualified())
      return false;
    if (E->isPointerType())
      return !E->getPointeeType()->isPointerType() && !E->isFunctionPointerType() &&
             !isOwned(E) && !hasOwners(E->getPointeeType());
    const RecordDecl *RD = E->getAsRecordDecl();
    RD = RD ? RD->getDefinition() : nullptr;
    return RD && mine(RD) && !RD->isUnion() &&
           llvm::all_of(RD->fields(), [&](const FieldDecl *F) {
             return !hasOwners(F->getType()) ||
                    pointerKind(F->getType()) == "static";
           });
  }

  /// What a function type promises about its pointers; nothing if it has no rule.
  std::optional<SmallVector<std::string, 4>> marks(const FunctionProtoType *P) const {
    if (!P || P->isVariadic())
      return std::nullopt;
    SmallVector<std::string, 4> Out;
    auto Add = [&](QualType T) {
      if (indirectView(T))
        return false;
      if (T->isPointerType()
              ? T->getPointeeType()->isPointerType() || T->isFunctionPointerType()
              : hasOwners(T))
        return false;
      Out.push_back(pointerKind(T).str());
      return true;
    };
    for (QualType T : P->getParamTypes())
      if (!Add(T))
        return std::nullopt;
    if (!Add(P->getReturnType()))
      return std::nullopt;
    return Out;
  }
  static const FunctionProtoType *pointedProto(QualType T) {
    return T->isFunctionPointerType()
               ? T->getPointeeType()->getAs<FunctionProtoType>()
               : nullptr;
  }

  /// A struct whose pointers are all functions or '_Static' can be shared: a
  /// store to one is checked wherever it is.
  bool isShareable(QualType T) const {
    T = Ctx.getBaseElementType(T);
    if (T->isPointerType())
      return false;
    const RecordDecl *RD = T->getAsRecordDecl();
    RD = RD ? RD->getDefinition() : nullptr;
    return RD && mine(RD) && !RD->isUnion() &&
           llvm::all_of(RD->fields(), [&](const FieldDecl *F) {
             QualType FT = F->getType(), E = Ctx.getBaseElementType(FT);
             return !hasOwners(FT) || isShareable(FT) ||
                    (E->isFunctionPointerType() && marks(pointedProto(E))) ||
                    (pointerKind(FT) == "static" && E->isPointerType() &&
                     !hasOwners(E->getPointeeType()));
           });
  }
  /// An initializer runs in no function body: the functions it names.
  void initializer(const Expr *Init) {
    struct Names : RecursiveASTVisitor<Names> {
      FileRules &R;
      llvm::SmallPtrSet<const Expr *, 4> Called;
      explicit Names(FileRules &R) : R(R) {}
      bool VisitCallExpr(CallExpr *C) {
        Called.insert(C->getCallee()->IgnoreParenImpCasts());
        return true;
      }
      bool VisitDeclRefExpr(DeclRefExpr *E) {
        if (Called.contains(E))
          return true;
        const auto *F = dyn_cast<FunctionDecl>(E->getDecl());
        if (F && (!R.mine(F) || F->getBuiltinID()))
          R.error(E->getExprLoc(), "a library's prototype says nothing of what it "
                                   "frees; write a small function that calls it");
        return true;
      }
    } W(*this);
    if (Init)
      W.TraverseStmt(const_cast<Expr *>(Init));
  }

  void field(const FieldDecl *F) {
    QualType T = F->getType(), E = Ctx.getBaseElementType(T);
    if (view(E) || indirectView(T))
      return error(F->getLocation(), "a '_View' lives only in locals and parameters; "
                                     "keep its fields here as owners or indexes");
    if (view(Ctx.getCanonicalTagType(F->getParent()))) {
      bool Plain = T->isFunctionPointerType()
                       ? marks(pointedProto(T)).has_value()
                       : T->isPointerType() && pointerKind(T).empty() &&
                             !T->getPointeeType()->isPointerType();
      if (hasOwners(T) && !Plain)
        error(F->getLocation(), "a '_View' holds borrows and numbers; a pointer here "
                                "has no mark and does not point to a pointer");
      return;
    }
    if (!E->isPointerType()) {
      if (F->getParent()->isUnion() && hasOwners(E))
        error(F->getLocation(), "a union holding owners has no checked access; "
                                "use one field per member");
      return;
    }
    if (E->isFunctionPointerType() && !F->getParent()->isUnion()) {
      if (!marks(pointedProto(E)))
        error(F->getLocation(), "this function pointer's type has no rule: its "
                                "parameters may not be pointers to pointers, "
                                "function pointers, '...' or structs that own "
                                "memory");
    } else if (F->getParent()->isUnion())
      error(F->getLocation(), "a union holding pointers has no checked access; "
                              "use one field per member");
    else if (pointerKind(T) == "static" && !hasOwners(E->getPointeeType()))
      return;
    else if (pointerKind(T) == "unchecked" && !E->getPointeeType()->isPointerType())
      return;
    else if (!ownedAtEveryLevel(T))
      error(F->getLocation(),
            "'" + F->getNameAsString() +
                "' stores a borrow, which nothing can end when its owner does; "
                "if it frees what it points to declare it '_Owned', if it is a "
            "literal or a global '_Static', otherwise "
                "pass the pointer as a parameter or keep an index; a back "
            "pointer may be '_Unchecked'");
  }
  void global(const VarDecl *V) {
    if (V->hasInit())
      initializer(V->getInit());
    if (!hasOwners(V->getType()) || isTable(V->getType()) || isShareable(V->getType()))
      return;
    bool Pointers = Ctx.getBaseElementType(V->getType())->isPointerType();
    error(V->getLocation(), "'" + V->getNameAsString() +
                                "' is shared by the whole program and holds a "
                                "pointer" +
                                (Pointers ? "; if it never changes put 'const' "
                                            "after the '*', otherwise keep it"
                                          : "; keep it") +
                                " in a struct that main owns and pass that");
  }
  void function(const FunctionDecl *FD) {
    auto Bad = [&](SourceLocation L, const Twine &M) { error(L, M); };
    QualType R = FD->getReturnType();
    if (R->isPointerType() && R->getPointeeType()->isPointerType())
      Bad(FD->getLocation(), "returns a pointer to a pointer, which has no rule; "
                             "return the inner pointer");
    if (R->isPointerType() && pointerKind(R) == "static" &&
        hasOwners(R->getPointeeType()))
      Bad(FD->getLocation(), "a '_Static' result points to nothing that owns memory");
    auto IsBorrow = [&](QualType T) {
      return view(T) || (T->isPointerType() && !T->isFunctionPointerType() &&
                         !isOwned(T) && !isTaken(T) && pointerKind(T) != "unchecked");
    };
    auto First = llvm::find_if(FD->parameters(), [&](const ParmVarDecl *P) {
      return IsBorrow(P->getType());
    });
    if (indirectView(R))
      Bad(FD->getLocation(), "a pointer to a '_View' is not returned; return the "
                             "'_View' itself");
    else if (const RecordDecl *V = view(R);
             V && (pointersIn(V) != 1 || First == FD->param_end()))
      Bad(FD->getLocation(), "a '_View' is returned only if it has one pointer, which "
                             "comes from the first parameter that is not '_Owned'");
    if (IsBorrow(R) && pointerKind(R) != "static" && First != FD->param_end()) {
      QualType T = (*First)->getType();
      const RecordDecl *V = view(T->isPointerType() ? T->getPointeeType() : T);
      if (V && pointersIn(V) != 1)
        Bad(FD->getLocation(), "which pointer of the '_View' the result comes from "
                               "cannot be told; pass that pointer");
    }
    if (!R->isPointerType() && hasOwners(R) && !view(R))
      Bad(FD->getLocation(), "a struct that owns memory is returned by value, "
                             "which copies its owners; fill one through a pointer");
    for (const ParmVarDecl *P : FD->parameters()) {
      QualType T = P->getType();
      if (T->isFunctionPointerType()) {
        if (!marks(pointedProto(T)))
          Bad(P->getLocation(), "this function pointer's type has no rule: its "
                                "parameters may not be pointers to pointers, "
                                "function pointers, '...' or structs that own "
                                "memory");
      } else if (T->isPointerType() && T->getPointeeType()->isPointerType() &&
               !FD->isMain())
        Bad(P->getLocation(), "a pointer to a pointer parameter has no rule; "
                              "pass the struct that holds the pointer");
      else if (changesView(T)) {
        unsigned Changed = llvm::count_if(FD->parameters(), [&](const ParmVarDecl *Q) {
          return changesView(Q->getType());
        });
        bool Takes = llvm::any_of(FD->parameters(), [&](const ParmVarDecl *Q) {
          return isTaken(Q->getType());
        });
        // (With several pointers, only its numbers change.)
        if (pointersIn(view(T->getPointeeType())) == 1 && (Changed > 1 || Takes || R->isPointerType() || hasOwners(R)))
          Bad(P->getLocation(), "a function that changes a '_View' changes one, "
                                "returns a number or nothing, and takes no owner on "
                                "success");
        else if (T->getNullability() == NullabilityKind::Nullable)
          Bad(P->getLocation(), "a '_View' that is changed is always there; remove "
                                "'_Nullable'");
      } else if (indirectView(T) && !constViewPointer(T))
        Bad(P->getLocation(), "a '_View' is passed by value or as a pointer");
      else if (!T->isPointerType() && hasOwners(T) && !view(T))
        Bad(P->getLocation(), "a struct that owns memory is passed by value, "
                              "which copies its owners; pass a pointer");
    }
    if (const FunctionDecl *Prev = FD->getPreviousDecl())
      if (pointerKind(Prev->getReturnType()) != pointerKind(R))
        Bad(FD->getLocation(), "an earlier declaration marks the result differently");
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
        if (V->hasInit() && (V->hasGlobalStorage() || T->isRecordType() ||
                             T->isArrayType()))
          R.initializer(V->getInit());
        if (V->hasGlobalStorage() && (R.isTable(T) || R.isShareable(T)))
          return true;
        if (V->isStaticLocal() && R.hasOwners(T))
          R.error(V->getLocation(), "a static local holding a pointer has no rule; "
                                    "pass it as a parameter");
        if (E->isFunctionPointerType() &&
            (T->isArrayType() || !R.marks(pointedProto(E))))
          R.error(V->getLocation(), "this function pointer's type has no rule; "
                                    "call the function by name");
        if (R.indirectView(T))
          R.error(V->getLocation(), "a '_View' is not kept in an array or behind a "
                                    "pointer; use the variable itself");
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
      bool VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
        R.initializer(E->getInitializer());
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
        // '(char *)malloc(n)' says nothing new.
        const Expr *From = C->getSubExpr()->IgnoreParenImpCasts();
        if (From->getType()->isVoidPointerType() && isa<CallExpr>(From))
          return true;
        // Between numbers, nothing is owned (bounds and alignment are checked
        // elsewhere).
        if (From->getType()->isPointerType() && C->getType()->isPointerType() &&
            From->getType()->getPointeeType()->isArithmeticType() &&
            C->getType()->getPointeeType()->isArithmeticType())
          return true;
        if (C->getType()->isPointerType() && !Invalid && *P == '(' &&
            !C->getSubExpr()->isNullPointerConstant(
                R.Ctx, Expr::NPC_ValueDependentIsNotNull))
          R.error(C->getExprLoc(), "a pointer cast has no ownership rule; declare "
                                   "the pointer with the type it has, or cast in a "
                                   "small function marked '_Trusted'");
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
    const RecordDecl *View = nullptr;
    // A view the program has no name for: its blocks are checked where it is
    // copied to one that has.
    bool Temporary = false;
  };
  llvm::DenseMap<Value, Var> Vars;
  llvm::DenseMap<Value, std::string> Globals;
  llvm::DenseSet<Value> Escapes, Temporaries;
  llvm::DenseMap<mlir::Block *, State> In;
  SmallVector<mlir::Block *, 16> Work;
  int FirstBorrow = -1;
  // The variable each pointer of a view is, named by the first access to it.
  std::map<std::pair<void *, const FieldDecl *>, Value> ViewFields;

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
    if (Failure)
      return;
    Failure = Rejected{Cur ? Cur->getLoc() : Fn.getLoc(), M.str()};
    if (!Cur)
      return;
    // A member access is where its field is declared: look around it.
    for (Value V : Cur->getResults())
      for (mlir::Operation *U : V.getUsers())
        Failure->Around.push_back(U->getLoc());
    for (Value V : Cur->getOperands())
      if (mlir::Operation *D = V.getDefiningOp())
        Failure->Around.push_back(D->getLoc());
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

  /// What the program calls the pointer V.
  std::string written(Value V, unsigned Depth = 0) {
    auto It = Vars.find(V);
    if (It != Vars.end() && !It->second.Name.empty())
      return It->second.Name;
    mlir::Operation *Op = V.getDefiningOp();
    if (!Op || Depth > 8)
      return "";
    if (auto L = mlir::dyn_cast<cir::LoadOp>(Op)) {
      if (auto M = L.getAddr().getDefiningOp<cir::GetMemberOp>();
          M && !Vars.count(L.getAddr())) {
        std::string B = written(M.getAddr(), Depth + 1);
        bool Dot = M.getAddr().getDefiningOp<cir::AllocaOp>();
        return B.empty() ? "" : B + (Dot ? "." : "->") + M.getName().str();
      }
      return written(L.getAddr(), Depth + 1);
    }
    if (mlir::isa<cir::CastOp, cir::PtrStrideOp>(Op))
      return written(Op->getOperand(0), Depth + 1);
    return "";
  }
  std::string ended(Value V, StringRef What, StringRef Why) {
    std::string Name = written(V);
    StringRef Is = llvm::StringSwitch<StringRef>(What)
                       .Case("use", "used")
                       .Case("write", "written through")
                       .Case("test", "tested")
                       .Case("copy", "copied")
                       .Case("change", "changed")
                       .Case("the result", "used")
                       .Default(What);
    bool Verb = false;
    for (StringRef W : {"freed", "closed", "moved", "stored", "passed", "returned", "copied"})
      Verb |= Why.starts_with(W);
    return (Name.empty() ? "this pointer" : "'" + Name + "'") + " is " + Is.str() +
           " after " + (Verb ? "it was " : "") + Why.str();
  }
  Node *live(Value V, const Twine &What) {
    Node *N = node(V);
    if (!N)
      fail(nameOf(V) + " may be unset here");
    else if (N->K == Node::Dead)
      fail(ended(V, What.str(), N->Why.empty() ? StringRef("it ended") : StringRef(N->Why)));
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
  /// Nothing made from a dead pointer is live.
  Node borrow(Value Parent, bool Capable) {
    Node N;
    if (Parent) {
      const Node *P = node(Parent);
      if (!P || P->K == Node::Dead || P->K == Node::Maybe) {
        N.Why = P ? P->Why : "its source ended";
        if (P && P->K == Node::Maybe)
          N.Why = "a call that may have taken it; test that call's result first";
        return N;
      }
    }
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
    case Val::Raw:
      fail("an '_Unchecked' pointer is followed only inside a '_Trusted' function");
      return std::nullopt;
    case Val::View:
    case Val::ViewAt:
      fail(What + ": a '_View' is used by its fields");
      return std::nullopt;
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

  /// A signal arrives in the middle of anything, 'malloc' included.
  void signalHandler(Value V) {
    auto It = Globals.find(V);
    const auto *F = It == Globals.end()
                        ? nullptr
                        : dyn_cast_or_null<FunctionDecl>(
                              Gen.getDeclForMangledName(It->second));
    const FunctionDecl *Def = F ? F->getDefinition() : nullptr;
    if (!Def)
      return fail("a signal handler is named, and defined in this file");
    if (R.trusted(Def))
      return;
    struct Calls : RecursiveASTVisitor<Calls> {
      const CallExpr *Bad = nullptr;
      bool VisitCallExpr(CallExpr *C) {
        const FunctionDecl *D = C->getDirectCallee();
        StringRef N = D && D->getIdentifier() ? D->getName() : StringRef();
        if (N != "_exit" && N != "write")
          Bad = C;
        return true;
      }
    } W;
    W.TraverseStmt(Def->getBody());
    if (W.Bad)
      fail("a signal may arrive in the middle of 'malloc' or 'printf', so a handler "
           "calls nothing but 'write' and '_exit'; set a 'volatile sig_atomic_t' and "
           "act on it later");
  }
  /// Is A a pointer of a view parameter that may not change it?
  bool fixedPart(Value A) {
    auto M = A.getDefiningOp<cir::GetMemberOp>();
    Val *X = M ? val(M.getAddr()) : nullptr;
    return M && ((X && X->K == Val::ViewAt && viewPointers(viewIn(X->Of)).size() != 1) ||
                 (Fn.getNumArguments() && llvm::is_contained(Fn.getArguments(), M.getAddr()) &&
                  viewPointers(viewIn(M.getAddr())).size() != 1));
  }
  /// The field an address is in, or is an element of.
  const FieldDecl *fieldOf(Value Addr) const {
    while (mlir::Operation *Op = Addr.getDefiningOp()) {
      if (auto M = mlir::dyn_cast<cir::GetMemberOp>(Op)) {
        auto RT = mlir::dyn_cast<cir::RecordType>(pointee(M.getAddr().getType()));
        auto It = RT && RT.getName() ? Records.find(RT.getName().getValue())
                                     : Records.end();
        if (It == Records.end() || M.getName().empty())
          return nullptr;
        for (const FieldDecl *F : It->second->fields())
          if (F->getName() == M.getName())
            return F;
        return nullptr;
      }
      auto C = mlir::dyn_cast<cir::CastOp>(Op);
      if (!(C && C.getKind() == cir::CastKind::array_to_ptrdecay) &&
          !mlir::isa<cir::PtrStrideOp, cir::GetElementOp>(Op))
        return nullptr;
      Addr = Op->getOperand(0);
    }
    return nullptr;
  }
  bool inStaticField(Value Addr) const {
    const FieldDecl *F = fieldOf(Addr);
    return F && pointerKind(F->getType()) == "static";
  }
  bool isStatic(Value V) {
    Val *X = val(V);
    if (!X)
      return false;
    if (X->K == Val::Null || X->K == Val::Static)
      return true;
    const Node *N = X->K == Val::Pointer ? node(X->Of) : nullptr;
    return N && N->K == Node::Static;
  }
  /// The type of the function pointer in a variable or a field.
  const FunctionProtoType *protoAt(Value Addr) const {
    auto It = Vars.find(Addr);
    if (It != Vars.end())
      return It->second.VD ? FileRules::pointedProto(It->second.VD->getType())
                           : nullptr;
    const FieldDecl *F = fieldOf(Addr);
    return F && R.mine(F) ? FileRules::pointedProto(
                                R.Ctx.getBaseElementType(F->getType()))
                          : nullptr;
  }
  const FunctionProtoType *calledThrough(Value V) const {
    auto L = V.getDefiningOp<cir::LoadOp>();
    return L ? protoAt(L.getAddr()) : nullptr;
  }
  static bool holdsFunction(mlir::Type T) {
    return isPtr(T) && mlir::isa<cir::FuncType>(pointee(T));
  }
  bool markedUnchecked(Value Addr) const {
    auto It = Vars.find(Addr);
    if (It != Vars.end())
      return It->second.VD && pointerKind(It->second.VD->getType()) == "unchecked";
    const FieldDecl *F = fieldOf(Addr);
    return F && pointerKind(F->getType()) == "unchecked";
  }
  /// Any pointer may become '_Unchecked' but an owner, which would be lost.
  void toUnchecked(Value V) {
    Val *X = val(V);
    if (!X)
      return fail("a pointer value with no ownership rule");
    if (X->K == Val::Raw)
      return;
    if (X->K == Val::Owner)
      return fail("this owner would be lost; keep it in an '_Owned' variable");
    if (X->K == Val::Function || (X->K == Val::Local && Vars.lookup(X->Of).Pointer))
      return fail("this has no rule as an '_Unchecked' pointer");
    base(V, "copied");
  }
  bool ours(const FunctionDecl *F) const {
    return F && R.mine(F) && !F->getBuiltinID();
  }
  /// Is V a function that promises exactly what Formal does? A library's
  /// prototype promises nothing, so its functions are not passed.
  void functionArgument(Value V, const FunctionProtoType *Formal) {
    auto Want = R.marks(Formal);
    if (!Want)
      return fail("this function pointer's type has no ownership rule");
    const FunctionProtoType *Got = calledThrough(V);
    if (!Got) {
      auto It = Globals.find(V);
      mlir::Operation *Op = V.getDefiningOp();
      while (It == Globals.end() && Op && mlir::isa<cir::CastOp>(Op)) {
        It = Globals.find(Op->getOperand(0));
        Op = Op->getOperand(0).getDefiningOp();
      }
      const auto *F = It == Globals.end()
                          ? nullptr
                          : dyn_cast_or_null<FunctionDecl>(
                                Gen.getDeclForMangledName(It->second));
      if (!ours(F) || libraryContract(It->second) != Lib::None)
        return fail("only a function of this program may be passed, by name or from "
                    "a parameter; write a small function that calls this one");
      Got = F->getType()->getAs<FunctionProtoType>();
    }
    if (!Got || !R.Ctx.hasSameType(QualType(Got, 0), QualType(Formal, 0)))
      return fail("this function's type differs from the parameter's");
    auto Has = R.marks(Got);
    if (!Has || *Has != *Want)
      fail("this function's parameters or result are marked differently from the "
           "function pointer's; write the same marks in both");
  }
  /// A constant's pointers are null, or the addresses of functions, globals and
  /// literals, where any of those may be.
  bool constantOk(mlir::Attribute A, mlir::Type T) const {
    bool Null = true, Known = true;
    A.walk([&](mlir::Attribute X) {
      if (auto P = mlir::dyn_cast<cir::ConstPtrAttr>(X))
        Known &= P.isNullValue();
      else if (mlir::isa<cir::GlobalViewAttr>(X))
        Null = false;
    });
    if (!Known || Null || viewOf(T))
      return Known;
    while (auto AT = mlir::dyn_cast<cir::ArrayType>(T))
      T = AT.getElementType();
    auto RT = mlir::dyn_cast<cir::RecordType>(T);
    auto It = RT && RT.getName() ? Records.find(RT.getName().getValue())
                                 : Records.end();
    return It != Records.end() &&
           R.isShareable(R.Ctx.getCanonicalTagType(It->second));
  }
  void assignBorrow(Value A, Value P, bool Cap);

  const RecordDecl *viewOf(mlir::Type T) const {
    auto RT = mlir::dyn_cast<cir::RecordType>(T);
    auto It = RT && RT.getName() ? Records.find(RT.getName().getValue())
                                 : Records.end();
    return It == Records.end()
               ? nullptr
               : R.view(R.Ctx.getCanonicalTagType(It->second));
  }
  /// The variable that F of the view in slot V is; none if it was never named,
  /// so never set.
  Value viewField(Value V, const FieldDecl *F) const {
    auto It = ViewFields.find({V.getAsOpaquePointer(), F});
    return It == ViewFields.end() ? Value() : It->second;
  }
  static SmallVector<const FieldDecl *, 2> viewPointers(const RecordDecl *RD) {
    SmallVector<const FieldDecl *, 2> Out;
    for (const FieldDecl *F : RD->fields())
      if (F->getType()->isPointerType() && !F->getType()->isFunctionPointerType())
        Out.push_back(F);
    return Out;
  }
  /// The slot a view was just read from.
  Value viewSlot(Value V) {
    auto L = V.getDefiningOp<cir::LoadOp>();
    if (!L || !Cur || L->getBlock() != Cur->getBlock())
      return Value();
    for (mlir::Operation *Q = L->getNextNode(); Q && Q != Cur; Q = Q->getNextNode())
      if (!mlir::isa<cir::LoadOp, cir::ConstantOp, cir::CastOp, cir::GetMemberOp,
                     cir::GetGlobalOp>(Q))
        return Value();
    Val *X = val(L.getAddr());
    return X && X->K == Val::Local && Vars[X->Of].View ? X->Of : Value();
  }
  const RecordDecl *viewIn(Value V) {
    if (const RecordDecl *RD = Vars.lookup(V).View)
      return RD;
    return isPtr(V.getType()) ? viewOf(pointee(V.getType())) : nullptr;
  }
  /// Every pointer of the view in slot V, live.
  bool viewParts(Value V, const Twine &What, SmallVectorImpl<Value> &Out) {
    for (const FieldDecl *F : viewPointers(viewIn(V))) {
      Value K = viewField(V, F);
      if (!K || !node(K)) {
        fail("'" + Vars.lookup(V).Name + "." + F->getNameAsString() + "' may be unset here");
        return false;
      }
      if (!live(K, What))
        return false;
      Out.push_back(K);
    }
    return true;
  }
  void assignPart(Value A, Value From) {
    Node *N = live(From, "copied");
    if (!N)
      return;
    if (N->K == Node::Static) {
      reparent(A);
      S->N[A] = plain(Node::Static);
      return;
    }
    assignBorrow(A, From, capable(N) && Vars[A].Capable);
  }
  /// The view in slot V gets a whole new value.
  void assignView(Value V, Value From) {
    const RecordDecl *RD = Vars[V].View;
    Val *X = val(From);
    if (auto C = From.getDefiningOp<cir::ConstantOp>()) {
      for (const FieldDecl *F : viewPointers(RD))
        if (Value K = viewField(V, F)) {
          reparent(K);
          S->N[K] = plain(Node::Static);
        }
      return;
    }
    if (!X || X->K != Val::View)
      return fail("a '_View' is set from another, or field by field");
    Value Slot = viewSlot(From);
    if (!Slot && !node(From))
      return fail("copy this '_View' into a variable first");
    if (!Slot && !live(From, "copied"))
      return;
    for (const FieldDecl *F : viewPointers(RD)) {
      Value K = viewField(V, F), P = Slot ? viewField(Slot, F) : From;
      if (Slot && (!P || !node(P)))
        return fail("'" + Vars[Slot].Name + "." + F->getNameAsString() +
                    "' may be unset here");
      assignPart(K, P);
      if (Failed())
        return;
    }
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
    case Val::Raw:
    case Val::View:
    case Val::ViewAt:
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
    X.View = viewOf(T);
    if (X.View) {
      X.Temporary = !X.VD && X.Name != "__retval" && !Parm;
      if (Op.isDynamic())
        return fail("a '_View' is not kept in an array");
    }
  }
  if (X.View)
    for (mlir::Operation *U : V.getUsers())
      if (auto M = mlir::dyn_cast<cir::GetMemberOp>(U))
        for (const FieldDecl *F : viewPointers(X.View))
          if (F->getName() == M.getName()) {
            Value K = ViewFields.try_emplace({V.getAsOpaquePointer(), F},
                                             M.getResult()).first->second;
            reparent(K);
            S->N.erase(K);
            Var P;
            P.Name = X.Name + "." + F->getNameAsString();
            P.Scope = X.Scope;
            P.Pointer = true;
            P.Temporary = X.Temporary;
            P.Capable = !F->getType()->getPointeeType().isConstQualified();
            Vars[K] = P;
            Vals[M.getResult()] = mk(Val::Local, K);
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
    if (!val(Src) || val(Src)->K != Val::Raw)
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
  if (From != To && (mlir::isa<cir::FuncType>(pointee(From)) ||
                     mlir::isa<cir::FuncType>(pointee(To))) &&
      !(val(Src) && val(Src)->K == Val::Null))
    return fail("a function pointer conversion has no ownership rule");
  Val *P = val(Src);
  if (!P)
    return fail("this value has no ownership rule");
  Val V = *P;
  mlir::Type F = pointee(From), T = pointee(To);
  if (V.K == Val::Null || V.K == Val::Function ||
      (V.K == Val::Raw && F == T)) {
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
    return llvm::all_of(Res.getUsers(), [&](mlir::Operation *U) {
      auto C = mlir::dyn_cast<cir::CallOp>(U);
      if (!C || !C.getCallee())
        return false;
      StringRef N = cName(*C.getCallee());
      // A function of this program that takes the bytes.
      const auto *Fn = dyn_cast_or_null<FunctionDecl>(
          Gen.getDeclForMangledName(*C.getCallee()));
      if (ours(Fn) && !ownersIn(F)) {
        unsigned I = 0;
        for (Value A : C.getArgOperands()) {
          if (A == Res && (I >= Fn->getNumParams() ||
                           !isOwned(Fn->getParamDecl(I)->getType())))
            return false;
          ++I;
        }
        return true;
      }
      // What realloc adds is not null.
      return N == "free" || (N == "realloc" && !ownersIn(F));
    });
  };
  if (ToVoid && OnlyFreed() && libraryRecord(F))
    return fail("the library made this and only its own function closes it, as "
                "'fclose' does");
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
  // Nothing turns a 'void *' back into a struct but a '_Trusted' function.
  bool Bytes = !mlir::isa<cir::RecordType>(F) || (ToVoid && !libraryRecord(F));
  if (!ownersIn(F) && !ownersIn(T) && Bytes && !mlir::isa<cir::RecordType>(T) &&
      V.K != Val::Owner) {
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
  fail("a pointer conversion between different types has no ownership rule; "
       "convert it in a small function marked '_Trusted'");
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
    if (X.View) {
      Vals[Res] = mk(Val::View);
      return;
    }
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
    if (const auto *P = dyn_cast_or_null<ParmVarDecl>(X.VD);
        P && R.changesView(P->getType())) {
      Vals[Res] = mk(Val::ViewAt, Fn.getArgument(P->getFunctionScopeIndex()));
      return;
    }
    reparent(Res);
    Node *N = node(A);
    if (!N)
      return fail(nameOf(A) + " may be unset here");
    if (markedUnchecked(A)) {
      Vals[Res] = mk(Val::Raw);
      return;
    }
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
  if (holdsFunction(T) ? calledThrough(Res) != nullptr : markedUnchecked(Addr)) {
    if (!holdsFunction(T)) {
      Vals[Res] = mk(Val::Raw);
      return;
    }
    setNode(Res, plain(Node::Static));
    Vals[Res] = mk(Val::Pointer, Res);
    return;
  }
  if (inStaticField(Addr) || (V->K == Val::Static && V->Table)) {
    setNode(Res, plain(Node::Static));
    Vals[Res] = mk(Val::Pointer, Res);
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

/// The pointer variable A now points into what P names.
void FunctionCheck::assignBorrow(Value A, Value P, bool Cap) {
  Var X = Vars[A];
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
  if (RIt != Vars.end() && !RIt->second.Pointer && !X.Temporary) {
    const Var &RV = RIt->second;
    if (StringRef(X.Name).starts_with("__retval") || X.Name.empty())
      return fail("this returns a pointer to " + nameOf(Root) +
                  ", which ends with the function");
    if (RV.Scope.empty() || X.Scope.empty())
      return fail(nameOf(A) + " points to a variable or temporary with no known "
                              "block");
    if (!StringRef(X.Scope).starts_with(RV.Scope))
      return fail(nameOf(A) + " outlives " + nameOf(Root) +
                  ", which it would point to; declare " + nameOf(Root) +
                  " in the same block or an outer one");
  }
  S->N[A] = borrow(P, Cap);
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
    if (X.View)
      return assignView(A, V);
    if (fixedPart(D) || fixedPart(A))
      return fail("a '_View' with several pointers keeps them while it is behind a "
                  "pointer; only its numbers change, or pass the pointer to change");
    if (!X.Pointer) {
      if (ownersIn(T)) {
        auto C = V.getDefiningOp<cir::ConstantOp>();
        if (!C || !constantOk(C.getValue(), T))
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
      if (const auto *P = dyn_cast_or_null<ParmVarDecl>(X.VD);
          P && R.changesView(P->getType())) {
        S->N[A] = plain(Node::Static);
        return;
      }
      Node *N = node(V);
      if (!N)
        return fail("this parameter has no ownership rule");
      if (X.Owned) {
        S->N.erase(V);
        S->N[A] = plain(Node::Own);
      } else if (markedUnchecked(A)) {
        S->N[A] = plain(Node::Static);
      } else if (N->K == Node::Static) {
        S->N[A] = plain(Node::Static);
      } else {
        bool Cap = N->Capable;
        S->N[A] = borrow(V, Cap);
      }
      return;
    }
    if (X.VD && X.VD->getType()->isFunctionPointerType()) {
      if (!val(V) || val(V)->K != Val::Null)
        functionArgument(V, FileRules::pointedProto(X.VD->getType()));
      reparent(A);
      S->N[A] = plain(Node::Static);
      return;
    }
    if (markedUnchecked(A)) {
      toUnchecked(V);
      reparent(A);
      S->N[A] = plain(Node::Static);
      return;
    }
    if (const auto *P = dyn_cast_or_null<ParmVarDecl>(X.VD);
        P && R.changesView(P->getType()))
      return fail("a parameter that may change a '_View' is not assigned");
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
    return assignBorrow(A, P, Cap);
  }
  if (!isPtr(T)) {
    if (!base(D, "write"))
      return;
    if (ownersIn(T))
      return fail("this copies a struct that owns memory; move its fields one by "
                  "one");
    return;
  }
  if (holdsFunction(T) || markedUnchecked(D)) {
    if (!base(D, "write"))
      return;
    if (!holdsFunction(T))
      return toUnchecked(V);
    const FunctionProtoType *Formal = protoAt(D);
    if (!Formal)
      return fail("a function pointer is stored where its type is not known");
    if (!val(V) || val(V)->K != Val::Null)
      functionArgument(V, Formal);
    return;
  }
  if (inStaticField(D)) {
    if (!base(D, "write"))
      return;
    if (!isStatic(V))
      fail("a '_Static' field holds only literals and the addresses of globals");
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
  // A call through a pointer: the rules read a signature, never a body, and a
  // function passed as that parameter has the same one.
  const FunctionProtoType *Through = nullptr;
  std::string PointerName;
  if (Op.isIndirect()) {
    Through = calledThrough(Op.getIndirectCall());
    if (!Through || !R.marks(Through))
      return fail("a call through a function pointer has no ownership rule unless the "
                  "pointer is a variable or a field; call the function by name");
    PointerName = "the function pointer";
  }
  StringRef Sym = Through ? StringRef() : *Op.getCallee();
  StringRef Name = Through ? StringRef(PointerName) : cName(Sym);
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
      Through ? nullptr
              : dyn_cast_or_null<FunctionDecl>(Gen.getDeclForMangledName(Sym));
  const bool Ours = Through || ours(Callee);
  const Lib K0 = Ours ? Lib::None : libraryContract(Name);
  const int OutArg = K0 == Lib::EndOut ? 1 : K0 == Lib::OwnerOut ? 0 : -1;
  auto NumParams = [&] {
    return Through ? Through->getNumParams() : Callee->getNumParams();
  };
  auto ParamType = [&](unsigned I) {
    return Through ? Through->getParamType(I) : Callee->getParamDecl(I)->getType();
  };
  mlir::OperandRange Args = Op.getArgOperands();
  bool ResPtr = Res && isPtr(Res.getType());
  if (!Ours)
    for (Value A : Args)
      if (viewOf(A.getType()))
        return fail("a '_View' is passed only to functions of this program");
  for (auto [I, A] : llvm::enumerate(Args)) {
    if (!isPtr(A.getType()))
      continue;
    Val *V = val(A);
    if (!V)
      return fail("this argument has no ownership rule");
    if (int(I) == OutArg && (V->K == Val::Local || V->K == Val::Null))
      continue;
    if (Ours && I < NumParams() && pointerKind(ParamType(I)) == "unchecked") {
      toUnchecked(A);
      if (Failed())
        return;
      continue;
    }
    if (V->K == Val::Local && Vars.lookup(V->Of).Pointer)
      return fail("the address of a pointer variable is passed, which has no rule; "
                  "pass the struct that holds the pointer, or return the pointer");
    if (V->K == Val::Function || mlir::isa<cir::FuncType>(pointee(A.getType()))) {
      const FunctionProtoType *Formal =
          I < (Through || Callee ? NumParams() : 0)
              ? FileRules::pointedProto(ParamType(I))
              : nullptr;
      if (V->K == Val::Null && Formal)
        continue;
      if (!Formal || !(Ours || K0 == Lib::Callback || K0 == Lib::InsideSecond))
        return fail("a function passed here has no ownership rule");
      functionArgument(A, Formal);
      if (!Failed() && !Ours && Name == "signal")
        signalHandler(A);
      if (Failed())
        return;
    }
  }
  auto NewOwner = [&](bool Zeroed, bool Typed) {
    setNode(Res, plain(Node::Own));
    Val N = mk(Val::Owner);
    N.Zeroed = Zeroed;
    N.Typed = Typed;
    Vals[Res] = N;
  };
  if (!Ours) {
    Lib K = K0;
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
    if (K == Lib::Reallocs) {
      takeOwner(Args[0], "passed to 'realloc' (which may free it)");
      return Failed() ? void() : NewOwner(false, false);
    }
    for (auto [I, A] : llvm::enumerate(Args)) {
      if (!isPtr(A.getType()) || int(I) == OutArg ||
          mlir::isa<cir::FuncType>(pointee(A.getType())))
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
    case Lib::EndOut:
    case Lib::OwnerOut: {
      if (ResPtr)
        return fail("the result of '" + Name + "' has no rule");
      if (Res)
        S->F.erase(Res);
      if (val(Args[OutArg])->K == Val::Null)
        return;
      Value A = val(Args[OutArg])->Of;
      Var X = Vars.lookup(A);
      if (!X.Pointer)
        return fail("this is not a pointer variable");
      if (K == Lib::OwnerOut) {
        if (!X.Owned)
          return fail("'" + Name + "' makes a new owner; declare " + nameOf(A) +
                      " '_Owned'");
        // A failed call may leave it as it was.
        const Node *N = node(A);
        if (!N || N->K != Node::Own)
          return fail("a failed '" + Name + "' may leave " + nameOf(A) +
                      " as it was; set it to NULL first");
        killDescendants(A, "'" + Name + "' gave its owner another value" + here());
        return;
      }
      if (X.Owned)
        return fail("'" + Name + "' makes " + nameOf(A) + " point into its first "
                    "argument, so it is not an owner");
      std::optional<Value> B = base(Args[0], "passed");
      if (!B)
        return;
      if (isStaticBase(B)) {
        reparent(A);
        S->N[A] = plain(Node::Static);
        return;
      }
      Val *From = val(Args[0]);
      return assignBorrow(A, From->K == Val::OwnerOf ? From->Of : *B, false);
    }
    case Lib::InsideFirst:
    case Lib::InsideSecond: {
      std::optional<Value> B =
          base(Args[K == Lib::InsideSecond ? 1 : 0], "passed");
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
      if (ResPtr && K == Lib::Callback && Res.use_empty())
        return;
      if (ResPtr)
        return fail("the result of '" + Name + "' has no rule");
      if (Res)
        S->F.erase(Res);
      return;
    }
  }
  // '...' is read by a '_Trusted' body, which frees and keeps none of it.
  for (unsigned I = NumParams(); I < Args.size(); ++I) {
    if (Through || !R.trusted(Callee))
      return fail("a function with '...' has no ownership rule unless it is "
                  "'_Trusted'");
    Value A = Args[I];
    if (!isPtr(A.getType())) {
      if (ownersIn(A.getType()))
        return fail("a struct that holds pointers is not passed to '...'");
      continue;
    }
    if (!base(A, "passed to '" + Name + "'"))
      return;
    if (ownersIn(pointee(A.getType())) || val(A)->K == Val::Owner)
      return fail("what owns memory is not passed to '...'");
  }
  struct Use {
    Value Base;
    bool Changes;
    unsigned Index;
  };
  SmallVector<Use, 4> Uses;
  SmallVector<Value, 2> Changed;
  for (unsigned I = 0; I < Args.size() && I < NumParams(); ++I) {
    QualType PT = ParamType(I);
    const RecordDecl *ByValue = R.view(PT);
    if (ByValue || R.constViewPointer(PT) || R.changesView(PT)) {
      const bool Changes = R.changesView(PT);
      Value Slot;
      if (ByValue) {
        Slot = viewSlot(Args[I]);
        if (!Slot)
          return fail("copy this '_View' into a variable first");
      } else {
        Val *X = val(Args[I]);
        if (X && (X->K == Val::ViewAt ||
                  (X->K == Val::Local && Vars.lookup(X->Of).View)))
          Slot = X->Of;
        if (!Slot && Changes)
          return fail("a '_View' that is changed is a variable or a parameter");
      }
      if (Slot) {
        SmallVector<Value, 2> Parts;
        if (!viewParts(Slot, "passed to '" + Name + "'", Parts))
          return;
        if (Changes && Parts.size() == 1)
          Changed.append(Parts.begin(), Parts.end());
        auto Fields = viewPointers(viewIn(Slot));
        for (auto [F, K] : llvm::zip(Fields, Parts)) {
          if (node(K)->K == Node::Static)
            continue;
          QualType Pointee = F->getType()->getPointeeType();
          Uses.push_back({K, (ByValue || Changes) && !Pointee.isConstQualified() &&
                                 R.hasOwners(Pointee), I});
        }
        continue;
      }
      // A 'const' pointer parameter handed on: an ordinary borrow.
    }
    if (!PT->isPointerType() || PT->isFunctionPointerType() || isOwned(PT) ||
        isTaken(PT) || pointerKind(PT) == "unchecked")
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
  for (unsigned I = 0; I < NumParams() && First < 0; ++I) {
    QualType PT = ParamType(I);
    if (R.view(PT) ||
        (PT->isPointerType() && !PT->isFunctionPointerType() && !isOwned(PT) &&
         !isTaken(PT) && pointerKind(PT) != "unchecked"))
      First = I;
  }
  const Use *From = nullptr;
  for (const Use &U : Uses)
    if (int(U.Index) == First)
      From = &U;
  bool FromCap = From && capable(node(From->Base));
  SmallVector<std::pair<Value, bool>, 2> Pending;
  for (unsigned I = 0; I < Args.size() && I < NumParams(); ++I) {
    QualType PT = ParamType(I);
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
  // 'x = f(x)': what borrowed from x itself now hangs off x's parent.
  for (Value K : Changed) {
    if (!live(K, "changed by '" + Name + "'"))
      return;
    Node N = *node(K);
    setNode(K, N);
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
  const bool ResView = viewOf(Res.getType());
  if (!ResPtr && !ResView) {
    Fact F;
    F.Token = Res;
    S->F[Res] = F;
    if (ownersIn(Res.getType()))
      fail("a struct that owns memory is returned by value");
    return;
  }
  QualType RT = Through ? Through->getReturnType() : Callee->getReturnType();
  // Bytes have whatever type they are first given, and are not known to be null.
  if (isOwned(RT))
    return NewOwner(!RT->isVoidPointerType(), !RT->isVoidPointerType());
  if (pointerKind(RT) == "unchecked") {
    Vals[Res] = mk(Val::Raw);
    return;
  }
  if (pointerKind(RT) == "static") {
    setNode(Res, plain(Node::Static));
    Vals[Res] = mk(Val::Pointer, Res);
    return;
  }
  if (!From) {
    if (First < 0)
      return fail("'" + Name + "' returns a borrow of nothing; if the caller frees "
                               "the result, mark it '_Owned'; if it is a literal or a "
                  "global, '_Static'");
    setNode(Res, plain(Node::Static));
    Vals[Res] = mk(ResView ? Val::View : Val::Pointer, Res);
    return;
  }
  if (!live(From->Base, "the result"))
    return;
  setNode(Res, borrow(From->Base, FromCap));
  Vals[Res] = mk(ResView ? Val::View : Val::Pointer, Res);
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
  for (const ParmVarDecl *P : FD->parameters()) {
    if (!R.changesView(P->getType()))
      continue;
    Value A = Fn.getArgument(P->getFunctionScopeIndex());
    SmallVector<Value, 2> Parts;
    if (!viewParts(A, "left in '" + P->getNameAsString() + "'", Parts))
      return;
    for (Value K : Parts)
      if (node(K)->K != Node::Static && rootOf(*S, K) != A)
        return fail("'" + P->getNameAsString() + "' is left with a pointer that is "
                    "not inside the one it came with");
  }
  if (V && viewOf(V.getType())) {
    Value K = viewSlot(V);
    if (K) {
      SmallVector<Value, 2> Parts;
      if (!viewParts(K, "returned", Parts))
        return;
      K = Parts.empty() ? Value() : Parts.front();
    } else {
      K = node(V) ? V : Value();
      if (!K)
        return fail("copy this '_View' into a variable first");
    }
    Node *N = live(K, "returned");
    if (!N)
      return;
    if (N->K != Node::Static &&
        (FirstBorrow < 0 || rootOf(*S, K) != Fn.getArgument(FirstBorrow)))
      return fail("the pointer of a returned '_View' comes from inside the first "
                  "parameter that is not '_Owned'");
  }
  if (V && isPtr(V.getType())) {
    Val *X = val(V);
    if (!X)
      return fail("the returned value has no ownership rule");
    if (isOwned(FD->getReturnType())) {
      takeOwner(V, "returned");
    } else if (pointerKind(FD->getReturnType()) == "unchecked") {
      toUnchecked(V);
    } else if (pointerKind(FD->getReturnType()) == "static") {
      if (!isStatic(V))
        return fail("a '_Static' result is a literal or the address of a global");
    } else if (X->K != Val::Null && X->K != Val::Static) {
      std::optional<Value> B = base(V, "returned");
      if (!B)
        return;
      if (!isStaticBase(B)) {
        Value Root = rootOf(*S, *B);
        if (FirstBorrow < 0 || Root != Fn.getArgument(FirstBorrow))
          return fail("a borrow may only be returned from inside the first "
                      "parameter that is not '_Owned'; if the caller frees it, "
                      "mark the result '_Owned'; if it is a literal or a "
                      "global, '_Static'");
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
        ownersIn(Res.getType()) && !constantOk(C.getValue(), Res.getType()))
      return fail("this initializer puts a pointer in a field that is not "
                  "'_Static', '_Unchecked' or a function pointer; set the field "
                  "afterwards");
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
    if (!D)
      for (auto &KV : Locals) // a static local is 'function.variable'
        if (KV.second.size() == 1 && KV.second.front().VD->isStaticLocal() &&
            G.getName() == (Fn.getSymName() + "." + KV.first()).str())
          D = KV.second.front().VD;
    Vals[Res].Table = D && R.mine(D) && R.isTable(D->getType());
    if (D && !Vals[Res].Table && !R.isShareable(D->getType()) && R.mine(D) && ownersIn(pointee(Res.getType())))
      fail("this global holds pointers");
    return;
  }
  if (mlir::isa<cir::GetMemberOp, cir::PtrStrideOp, cir::GetElementOp>(Op)) {
    Value Src = Op->getOperand(0), Res = Op->getResult(0);
    Val *V = val(Src);
    if (!V)
      return fail("this value has no ownership rule");
    if (V->K == Val::ViewAt) {
      Value Of = V->Of;
      auto M = mlir::dyn_cast<cir::GetMemberOp>(Op);
      if (!M)
        return fail("a '_View' parameter points to one '_View'");
      for (const FieldDecl *F : viewPointers(viewIn(Of)))
        if (F->getName() == M.getName()) {
          Value K = viewField(Of, F);
          if (!K) { // the first, made by run()
            K = ViewFields[{Of.getAsOpaquePointer(), F}] = Res;
            Var P;
            P.Name = F->getNameAsString();
            P.Pointer = true;
            P.Capable = !F->getType()->getPointeeType().isConstQualified();
            Vars[K] = P;
          }
          if (K == Res)
            S->N[K] = borrow(Of, Vars[K].Capable);
          Vals[Res] = mk(Val::Local, K);
          return;
        }
      setNode(Res, borrow(Of, true));
      Vals[Res] = mk(Val::Address, Res);
      return;
    }
    if (V->K == Val::Local) {
      if (Vars.lookup(V->Of).Pointer)
        return fail("the address of a pointer variable has no rule");
      Value Of = V->Of;
      if (const RecordDecl *RD = Vars[Of].View)
        if (auto M = mlir::dyn_cast<cir::GetMemberOp>(Op))
          for (const FieldDecl *F : viewPointers(RD))
            if (F->getName() == M.getName()) {
              Value K = viewField(Of, F);
              if (!K)
                return fail("this '_View' has no rule here");
              Vals[Res] = mk(Val::Local, K);
              return;
            }
      setNode(Res, borrow(Of, true));
      Vals[Res] = mk(Val::Address, Res);
      return;
    }
    if (V->K == Val::Static) {
      Val Copy = *V;
      Vals[Res] = Copy;
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
      for (Value X : {L, Rh})
        if (!Failed() && !(val(X) && val(X)->K == Val::Raw))
          base(X, "compared");
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
  if (N == "select" && Op->getNumOperands() == 3 && isPtr(Op->getResult(0).getType())) {
    // 'c ? p : q' where at most one can end.
    Value Res = Op->getResult(0), From;
    for (Value X : {Op->getOperand(1), Op->getOperand(2)}) {
      if (isStatic(X))
        continue;
      if (From || holdsFunction(X.getType()))
        return fail("this value flows through '?:', which has no rule");
      From = X;
    }
    if (!From) {
      setNode(Res, plain(Node::Static));
      Vals[Res] = mk(Val::Pointer, Res);
      return;
    }
    Val *X = val(From);
    if (!X || X->K == Val::Owner || X->K == Val::Raw || X->K == Val::Local)
      return fail("this value flows through '?:', which has no rule");
    std::optional<Value> B = base(From, "used");
    if (!B || !*B)
      return;
    setNode(Res, borrow(*B, capable(node(*B))));
    Vals[Res] = mk(Val::Pointer, Res);
    return;
  }
  if (N == "trap" || N == "unreachable")
    return;
  if (N == "copy") {
    auto Slot = [&](Value V) {
      Val *X = val(V);
      return X && X->K == Val::Local && Vars[X->Of].View ? X->Of : Value();
    };
    Value D = Slot(Op->getOperand(0)), F = Slot(Op->getOperand(1));
    if (D && F) {
      for (const FieldDecl *FD : viewPointers(Vars[D].View)) {
        Value K = viewField(D, FD), P = viewField(F, FD);
        if (!K && !P)
          continue;
        if (!K || !P || !node(P))
          return fail("'" + Vars[F].Name + "." + FD->getNameAsString() +
                      "' may be unset here");
        assignPart(K, P);
        if (Failed())
          return;
      }
      return;
    }
  }
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
  // Name every pointer of every view, used or not.
  SmallVector<cir::AllocaOp, 4> Views;
  Fn.walk([&](cir::AllocaOp A) {
    if (viewOf(A.getAllocaType()))
      Views.push_back(A);
  });
  if (Fn.getNumArguments() == FD->getNumParams())
    for (const ParmVarDecl *P : FD->parameters())
      if (R.changesView(P->getType())) {
        Value A = Fn.getArgument(P->getFunctionScopeIndex());
        auto RT = isPtr(A.getType())
                      ? mlir::dyn_cast<cir::RecordType>(pointee(A.getType()))
                      : cir::RecordType();
        mlir::OpBuilder B(Fn.getContext());
        B.setInsertionPointToStart(&Fn.getBody().front());
        for (const FieldDecl *F : viewPointers(R.view(P->getType()->getPointeeType()))) {
          unsigned I = F->getFieldIndex();
          if (!RT || I >= RT.getMembers().size() || !isPtr(RT.getMembers()[I])) {
            fail("the layout of this '_View' has no rule");
            return Failure;
          }
          cir::GetMemberOp::create(B, Fn.getLoc(),
                                   cir::PointerType::get(RT.getMembers()[I]), A,
                                   F->getName(), I);
        }
      }
  for (cir::AllocaOp A : Views) {
    auto RT = mlir::cast<cir::RecordType>(A.getAllocaType());
    mlir::OpBuilder B(A->getContext());
    B.setInsertionPointAfter(A);
    for (const FieldDecl *F : viewPointers(viewOf(RT))) {
      unsigned I = F->getFieldIndex();
      if (I >= RT.getMembers().size() || !isPtr(RT.getMembers()[I])) {
        fail("the layout of this '_View' has no rule");
        return Failure;
      }
      cir::GetMemberOp::create(B, A.getLoc(), cir::PointerType::get(RT.getMembers()[I]),
                               A.getResult(), F->getName(), I);
    }
  }
  mlir::Block &Entry = Fn.getBody().front();
  State St;
  for (unsigned I = 0; I < FD->getNumParams(); ++I) {
    QualType T = FD->getParamDecl(I)->getType();
    const RecordDecl *View = R.view(T);
    if (!T->isPointerType() && !View)
      continue;
    if (I >= Entry.getNumArguments()) {
      fail("this function's parameters have no ownership rule");
      return Failure;
    }
    Value A = Entry.getArgument(I);
    if (R.changesView(T)) {
      if (Fn.getNumArguments() != FD->getNumParams()) {
        fail("this function's parameters have no ownership rule");
        return Failure;
      }
      St.N[A] = borrow(Value(), llvm::any_of(viewPointers(R.view(T->getPointeeType())),
                                             [](const FieldDecl *F) {
        return !F->getType()->getPointeeType().isConstQualified();
      }));
      Vals[A] = mk(Val::ViewAt, A);
      continue;
    }
    if (View) {
      // One root for all its pointers, which ends more than it must.
      if (Fn.getNumArguments() != FD->getNumParams() || !viewOf(A.getType())) {
        fail("this function's parameters have no ownership rule");
        return Failure;
      }
      if (FirstBorrow < 0)
        FirstBorrow = I;
      bool Cap = llvm::any_of(viewPointers(View), [](const FieldDecl *F) {
        return !F->getType()->getPointeeType().isConstQualified();
      });
      St.N[A] = borrow(Value(), Cap);
      Vals[A] = mk(Val::View, A);
      continue;
    }
    if (T->isFunctionPointerType() || pointerKind(T) == "unchecked") {
      St.N[A] = plain(Node::Static);
      Vals[A] = mk(Val::Pointer, A);
      continue;
    }
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

/// A member access carries where its field is declared, which is outside the
/// function.
SourceLocation sourceOf(ASTContext &Ctx, mlir::Location L, const FunctionDecl *In) {
  SourceManager &SM = Ctx.getSourceManager();
  SourceLocation Out;
  L->walk([&](mlir::Location X) {
    auto F = mlir::dyn_cast<mlir::FileLineColLoc>(X);
    if (!F)
      return mlir::WalkResult::advance();
    SourceLocation Here;
    if (auto File = SM.getFileManager().getOptionalFileRef(F.getFilename().getValue()))
      Here = SM.translateFileLineCol(&File->getFileEntry(), F.getLine(),
                                   F.getColumn());
    if (Here.isInvalid())
      return mlir::WalkResult::advance();
    SourceRange R = In->getSourceRange();
    bool Inside = SM.isPointWithin(Here, SM.getExpansionLoc(R.getBegin()),
                                   SM.getExpansionLoc(R.getEnd()));
    if (Inside)
      Out = Here;
    return Inside ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
  });
  return Out;
}

void Consumer::HandleTranslationUnit(ASTContext &Ctx) {
  DiagnosticsEngine &Diags = CI.getDiagnostics();
  if (Diags.hasErrorOccurred())
    return;
  FileRules R(CI.getSema(), CI.getHeaderSearchOpts().ResourceDir);
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
      SourceLocation L = sourceOf(Ctx, Bad->Loc, Def);
      for (mlir::Location A : Bad->Around)
        if (L.isInvalid())
          L = sourceOf(Ctx, A, Def);
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
