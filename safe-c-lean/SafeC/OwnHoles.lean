-- Ownership rules: for each rule, a program that passes the checker with that
-- rule off and reaches use after free or double free; then the six C idioms
-- under the full rules. All checked by evaluation in the kernel.
import SafeC.Own

namespace SafeC.Own

open SafeC.Own.Stmt SafeC.Own.Expr

def seqs : List Stmt → Stmt
  | [] => .skip
  | [s] => s
  | s :: ss => .seq s (seqs ss)

def ok (R : Rules) (P : Prog) (D : Decls) (s : Stmt) : Bool :=
  P.ok R && (chkS R P D .empty s).isSome

/-- struct 0: list node `{ int v; _Owned node *next; }`. struct 1: `{ int v; }`.
struct 2: index-linked node `{ int v; int prev; int next; }`. -/
def S : List (List FTy) := [[.int, .own 0], [.int], [.int, .int, .int]]
def P0 : Prog := ⟨S, []⟩

def uaf (o : Out) : Bool := o.ubKind == some .useAfterFree
def dfree (o : Out) : Bool := o.ubKind == some .doubleFree

/-! ## Each rule is necessary -/

/-- Moved-from owner stays usable: `q = p; free(q); free(p);` -/
def cMoveD : Decls := [(0, .own 1), (1, .own 1)]
def cMove : Stmt := seqs [malloc 0 1 (lit 1), move 1 0, free 1, free 0]
theorem need_moveKills :
    ok { Rules.full with moveKills := false } P0 cMoveD cMove = true ∧
    dfree (run P0 cMove) = true ∧ ok Rules.full P0 cMoveD cMove = false := by decide

/-- Free does not end loans: `r = p; free(p); r->v;` -/
def cFreeD : Decls := [(0, .own 1), (1, .ref 1), (2, .int)]
def cFree : Stmt := seqs [malloc 0 1 (lit 1), borrow 1 0 (lit 0), free 0, load 2 1 (lit 0) 0]
theorem need_freeKills :
    ok { Rules.full with freeKills := false } P0 cFreeD cFree = true ∧
    uaf (run P0 cFree) = true ∧ ok Rules.full P0 cFreeD cFree = false := by decide

/-- Taking an owned field out does not end the owner's loans:
`r = x->next; n = x->next; x->next = NULL; free(n); r->v;` -/
def cTakeD : Decls := [(0, .own 0), (1, .own 0), (2, .ref 0), (3, .own 0), (4, .int)]
def cTake : Stmt := seqs [malloc 0 0 (lit 1), malloc 1 0 (lit 1), put 0 (lit 0) 1 1,
  load 2 0 (lit 0) 1, take 3 0 (lit 0) 1, free 3, load 4 2 (lit 0) 0]
theorem need_takeKills :
    ok { Rules.full with takeKills := false } P0 cTakeD cTake = true ∧
    uaf (run P0 cTake) = true ∧ ok Rules.full P0 cTakeD cTake = false := by decide

/-- Idiom 3 as C programmers write it: a pointer into a growable array held
across the `realloc` of a push. -/
def cReallocD : Decls := [(0, .own 1), (1, .ref 1), (2, .int)]
def cRealloc : Stmt := seqs [malloc 0 1 (lit 2), borrow 1 0 (lit 1), realloc 0 1 (lit 4), load 2 1 (lit 0) 0]
theorem need_reallocKills :
    ok { Rules.full with reallocKills := false } P0 cReallocD cRealloc = true ∧
    uaf (run P0 cRealloc) = true ∧ ok Rules.full P0 cReallocD cRealloc = false := by decide

/-- Free through a borrow: `r = p; free(r); free(p);` -/
def cFreeRef : Stmt := seqs [malloc 0 1 (lit 1), borrow 1 0 (lit 0), free 1, free 0]
theorem need_owner_free :
    ok { Rules.full with ownOpsNeedOwner := false } P0 cFreeD cFreeRef = true ∧
    dfree (run P0 cFreeRef) = true ∧ ok Rules.full P0 cFreeD cFreeRef = false := by decide

/-- Restructuring through a shared borrow, across a call: the callee takes
and frees `p->next` through its borrowed `p`, while the caller still holds
another borrow `r2 = x->next`. The callee cannot see the caller's loans, so
this is where aliasing xor mutation is forced: changing ownership through a
borrow needs that borrow to be exclusive. -/
def steal : Fn := ⟨[0], [(0, .ref 0), (1, .own 0), (2, .int)],
  seqs [take 1 0 (lit 0) 1, free 1, set 2 (lit 0)], 2, none⟩
def PSteal : Prog := ⟨S, [steal]⟩
def cStealD : Decls := [(0, .own 0), (1, .own 0), (2, .ref 0), (3, .ref 0), (4, .int), (5, .int)]
def cSteal : Stmt := seqs [malloc 0 0 (lit 1), malloc 1 0 (lit 1), put 0 (lit 0) 1 1,
  load 2 0 (lit 0) 1, borrow 3 0 (lit 0), call 5 0 [3], load 4 2 (lit 0) 0]
theorem need_owner_take :
    ok { Rules.full with ownOpsNeedOwner := false } PSteal cStealD cSteal = true ∧
    uaf (run PSteal cSteal) = true ∧ PSteal.ok Rules.full = false := by decide

/-- An owned argument is not consumed: the callee frees it, then the caller. -/
def sink : Fn := ⟨[0], [(0, .own 1), (1, .int)], seqs [free 0, set 1 (lit 0)], 1, none⟩
def PSink : Prog := ⟨S, [sink]⟩
def cSinkD : Decls := [(0, .own 1), (1, .int)]
def cSink : Stmt := seqs [malloc 0 1 (lit 1), call 1 0 [0], free 0]
theorem need_argsConsume :
    ok { Rules.full with argsConsume := false } PSink cSinkD cSink = true ∧
    dfree (run PSink cSink) = true ∧ ok Rules.full PSink cSinkD cSink = false := by decide

/-- `f(r, x)` where `r` borrows `x` and `x` moves into `f`: the callee frees
its owned parameter and reads the borrowed one. The callee is fine on its own
(`PBoth.ok Rules.full`); the rule belongs at the call site. -/
def both : Fn := ⟨[0, 1], [(0, .ref 1), (1, .own 1), (2, .int)],
  seqs [free 1, load 2 0 (lit 0) 0], 2, none⟩
def PBoth : Prog := ⟨S, [both]⟩
def cBothD : Decls := [(0, .own 1), (1, .ref 1), (2, .int)]
def cBoth : Stmt := seqs [malloc 0 1 (lit 1), borrow 1 0 (lit 0), call 2 0 [1, 0]]
theorem need_argsAfterMoves :
    ok { Rules.full with argsAfterMoves := false } PBoth cBothD cBoth = true ∧
    uaf (run PBoth cBoth) = true ∧ PBoth.ok Rules.full = true ∧
    ok Rules.full PBoth cBothD cBoth = false := by decide

/-- A returned borrow not tied to its argument: `r = id(&*p); free(p); r->v;` -/
def idFn (b : Option Nat) : Fn := ⟨[0], [(0, .ref 1)], .skip, 0, b⟩
def cIdD : Decls := [(0, .own 1), (1, .ref 1), (2, .ref 1), (3, .int)]
def cId : Stmt := seqs [malloc 0 1 (lit 1), borrow 1 0 (lit 0), call 2 0 [1], free 0, load 3 2 (lit 0) 0]
theorem need_retBound :
    ok { Rules.full with retBound := false } ⟨S, [idFn none]⟩ cIdD cId = true ∧
    uaf (run ⟨S, [idFn none]⟩ cId) = true ∧
    (⟨S, [idFn none]⟩ : Prog).ok Rules.full = false ∧
    ok Rules.full ⟨S, [idFn (some 0)]⟩ cIdD cId = false := by decide

/-! ## The M2 rules: exclusive borrows, stack locals, unchecked pointers, views, globals

struct 3: `{ int v; T *p; }` with a borrow field (rejected under `noRefFields`).
struct 4: `{ _Owned T *d; int n; }`, the growable vector of p04 and the `struct B`
of q02. struct 5: `{ T *__safec_unchecked g; }`.

Globals are one struct `G` held by `main` as a stack local and passed to every
function that touches a global as an exclusive parameter (see OWNERSHIP.md). -/

def S2 : List (List FTy) := S ++ [[.int, .ref 1], [.own 1, .int], [.unc 1]]
def P2 : Prog := ⟨S2, []⟩

def dstack (o : Out) : Bool := o.ubKind == some .danglingStack

/-- p01 and x12 (heap slot): a borrow stored in a heap struct outlives its
owner. `a->p = &*b; free(b); n = a->p; n->v;` -/
def cRefHeapD : Decls := [(0, .own 3), (1, .own 1), (2, .ref 1), (3, .ref 1), (4, .int)]
def cRefHeap : Stmt := seqs [malloc 0 3 (lit 1), malloc 1 1 (lit 1), borrow 2 1 (lit 0),
  pstore 0 (lit 0) 1 2, free 1, load 3 0 (lit 0) 1, load 4 3 (lit 0) 0]
theorem need_noRefFields_heap :
    ok { Rules.full with noRefFields := false } P2 cRefHeapD cRefHeap = true ∧
    uaf (run P2 cRefHeap) = true ∧ ok Rules.full P2 cRefHeapD cRefHeap = false := by decide

/-- q01: `stash(int *p) { g = p; }` stores a borrowed parameter in a global;
the caller frees the block and reads it back through the global. -/
def stash : Fn := ⟨[0, 1], [(0, .excl 3), (1, .ref 1), (2, .int)],
  seqs [pstore 0 (lit 0) 1 1, set 2 (lit 0)], 2, none⟩
def PStash : Prog := ⟨S2, [stash]⟩
def cStashD : Decls := [(0, .excl 3), (1, .own 1), (2, .ref 1), (3, .ref 1), (4, .int), (5, .int)]
def cStash : Stmt := scope 0 3 (lit 1) (seqs [malloc 1 1 (lit 1), borrow 2 1 (lit 0),
  call 5 0 [0, 2], free 1, load 3 0 (lit 0) 1, load 4 3 (lit 0) 0])
theorem need_noRefFields_global :
    ok { Rules.full with noRefFields := false } PStash cStashD cStash = true ∧
    uaf (run PStash cStash) = true ∧ PStash.ok Rules.full = false := by decide

/-- s03 (`fill`): a callee stores the address of its own stack local into a
struct field it was given; the caller reads it after the callee's scope died. -/
def fill : Fn := ⟨[0], [(0, .ref 3), (1, .excl 1), (2, .ref 1), (3, .int)],
  seqs [scope 1 1 (lit 64) (seqs [borrow 2 1 (lit 32), pstore 0 (lit 0) 1 2]), set 3 (lit 0)], 3, none⟩
def PFill : Prog := ⟨S2, [fill]⟩
def cFillD : Decls := [(0, .own 3), (1, .ref 3), (2, .ref 1), (3, .int), (4, .int)]
def cFill : Stmt := seqs [malloc 0 3 (lit 1), borrow 1 0 (lit 0), call 3 0 [1],
  load 2 0 (lit 0) 1, load 4 2 (lit 0) 0]
theorem need_noRefFields_stack :
    ok { Rules.full with noRefFields := false } PFill cFillD cFill = true ∧
    dstack (run PFill cFill) = true ∧ PFill.ok Rules.full = false := by decide

/-- q01 with the global made `__safec_unchecked`: storing is fine, the
dereference is what needs `__unsafe`. -/
def stashU : Fn := ⟨[0, 1], [(0, .excl 5), (1, .ref 1), (2, .int)],
  seqs [pstore 0 (lit 0) 0 1, set 2 (lit 0)], 2, none⟩
def PStashU : Prog := ⟨S2, [stashU]⟩
def cStashUD : Decls := [(0, .excl 5), (1, .own 1), (2, .ref 1), (3, .unc 1), (4, .int), (5, .int)]
def cStashUSafe : List Stmt := [malloc 1 1 (lit 1), borrow 2 1 (lit 0), call 5 0 [0, 2], free 1,
  load 3 0 (lit 0) 0]
def cStashU : Stmt := scope 0 5 (lit 1) (seqs (cStashUSafe ++ [load 4 3 (lit 0) 0]))
theorem need_uncNeedsUnsafe :
    ok { Rules.full with uncNeedsUnsafe := false } PStashU cStashUD cStashU = true ∧
    uaf (run PStashU cStashU) = true ∧ ok Rules.full PStashU cStashUD cStashU = false ∧
    ok Rules.full PStashU cStashUD (scope 0 5 (lit 1) (seqs cStashUSafe)) = true := by decide

/-- p04: `first = &v.d[0]; grow(&v); *first`, where `grow` reallocates `v->d`
through an exclusive borrow (`take`, `realloc`, `put`). -/
def growFn : Fn := ⟨[0], [(0, .excl 4), (1, .own 1), (2, .int)],
  seqs [take 1 0 (lit 0) 0, realloc 1 1 (lit 4), put 0 (lit 0) 0 1, set 2 (lit 0)], 2, none⟩
def PGrow : Prog := ⟨S2, [growFn]⟩
def cGrowD : Decls := [(0, .excl 4), (1, .own 1), (2, .ref 1), (3, .int), (4, .int)]
def cGrowPre : List Stmt := [malloc 1 1 (lit 2), put 0 (lit 0) 0 1, load 2 0 (lit 0) 0,
  store 2 (lit 0) 0 (lit 9), call 3 0 [0]]
def cGrow : Stmt := scope 0 4 (lit 1) (seqs (cGrowPre ++ [load 4 2 (lit 0) 0]))
theorem need_lendKills :
    ok { Rules.full with lendKills := false } PGrow cGrowD cGrow = true ∧
    uaf (run PGrow cGrow) = true ∧ PGrow.ok Rules.full = true ∧
    ok Rules.full PGrow cGrowD cGrow = false := by decide

/-- p04 written the M2 way: borrow `v.d` again after `grow`. -/
def cGrowOK : Stmt := scope 0 4 (lit 1) (seqs (cGrowPre ++ [load 2 0 (lit 0) 0, load 4 2 (lit 1) 0]))
theorem grow_reborrow_ok : ok Rules.full PGrow cGrowD cGrowOK = true ∧ (run PGrow cGrowOK).isUB = false := by
  decide

/-- Two exclusive arguments on one path: `two(&v, &v)`. The callee is fine on
its own; it borrows `q->d`, frees `p->d`, reads the borrow. -/
def twoFn : Fn := ⟨[0, 1], [(0, .excl 4), (1, .excl 4), (2, .ref 1), (3, .own 1), (4, .int)],
  seqs [load 2 1 (lit 0) 0, take 3 0 (lit 0) 0, free 3, load 4 2 (lit 0) 0], 4, none⟩
def PTwo : Prog := ⟨S2, [twoFn]⟩
def cTwoD : Decls := [(0, .excl 4), (1, .own 1), (2, .int)]
def cTwo : Stmt := scope 0 4 (lit 1) (seqs [malloc 1 1 (lit 1), put 0 (lit 0) 0 1, call 2 0 [0, 0]])
theorem need_lendOnce :
    ok { Rules.full with lendOnce := false } PTwo cTwoD cTwo = true ∧
    uaf (run PTwo cTwo) = true ∧ PTwo.ok Rules.full = true ∧
    ok Rules.full PTwo cTwoD cTwo = false := by decide

/-- x12 (scope): `{ int x[64]; q = &x[5]; } *q;` -/
def cScopeD : Decls := [(0, .excl 1), (1, .ref 1), (2, .int)]
def cScope : Stmt := seqs [scope 0 1 (lit 64) (borrow 1 0 (lit 5)), load 2 1 (lit 0) 0]
theorem need_stackKills :
    ok { Rules.full with stackKills := false } P2 cScopeD cScope = true ∧
    dstack (run P2 cScope) = true ∧ ok Rules.full P2 cScopeD cScope = false := by decide

/-- q02: `struct B c = a;` copies an owned field; `drop(&a); drop(&c);` frees
it twice. -/
def dropFn : Fn := ⟨[0], [(0, .excl 4), (1, .own 1), (2, .int)],
  seqs [take 1 0 (lit 0) 0, free 1, set 2 (lit 0)], 2, none⟩
def PDrop : Prog := ⟨S2, [dropFn]⟩
def cDropD : Decls := [(0, .excl 4), (1, .excl 4), (2, .own 1), (3, .int)]
def cDrop : Stmt := scope 0 4 (lit 1) (scope 1 4 (lit 1)
  (seqs [malloc 2 1 (lit 1), put 0 (lit 0) 0 2, copy 1 0, call 3 0 [0], call 3 0 [1]]))
theorem need_copyPlain :
    ok { Rules.full with copyPlain := false } PDrop cDropD cDrop = true ∧
    dfree (run PDrop cDrop) = true ∧ PDrop.ok Rules.full = true ∧
    ok Rules.full PDrop cDropD cDrop = false := by decide

/-- Globals: a borrow of a global survives a call that does not take `G`
(a pure callee) and dies at a call that does. -/
def pureFn : Fn := ⟨[], [(0, .int)], set 0 (lit 1), 0, none⟩
def PGlob : Prog := ⟨S2, [growFn, pureFn]⟩
def cGlobPre : List Stmt := [malloc 1 1 (lit 2), put 0 (lit 0) 0 1, load 2 0 (lit 0) 0, call 3 1 [],
  load 4 2 (lit 0) 0]
def cGlob : Stmt := scope 0 4 (lit 1) (seqs cGlobPre)
def cGlobStale : Stmt := scope 0 4 (lit 1) (seqs (cGlobPre ++ [call 3 0 [0], load 4 2 (lit 0) 0]))
theorem globals_pure_impure : ok Rules.full PGlob cGrowD cGlob = true ∧ (run PGlob cGlob).isUB = false ∧
    ok Rules.full PGlob cGrowD cGlobStale = false := by decide

/-- A borrow derived from a global passed to a callee that also takes `G`:
`use(r = &G.d[0])` where `use` frees `G.d` and reads `r`. Ending global loans
at the call is not enough; such a borrow may not be an argument of an impure
call at all (`argsAfterMoves` applied to the implicit lend of `G`). -/
def useG : Fn := ⟨[0, 1], [(0, .excl 4), (1, .ref 1), (2, .own 1), (3, .int)],
  seqs [take 2 0 (lit 0) 0, free 2, load 3 1 (lit 0) 0], 3, none⟩
def PUseG : Prog := ⟨S2, [useG]⟩
def cUseGD : Decls := [(0, .excl 4), (1, .own 1), (2, .ref 1), (3, .int)]
def cUseG : Stmt := scope 0 4 (lit 1) (seqs [malloc 1 1 (lit 1), put 0 (lit 0) 0 1, load 2 0 (lit 0) 0,
  call 3 0 [0, 2]])
theorem need_argsAfterMoves_global :
    ok { Rules.full with argsAfterMoves := false } PUseG cUseGD cUseG = true ∧
    uaf (run PUseG cUseG) = true ∧ PUseG.ok Rules.full = true ∧
    ok Rules.full PUseG cUseGD cUseG = false := by decide

/-! ## Idioms under the full rules -/

/-! ### 1. Singly linked list: push, traverse, pop, free all -/

def lD : Decls := [(0, .own 0), (1, .own 0), (2, .ref 0), (3, .int), (4, .int)]
/-- `n = calloc; n->v = k; n->next = head; head = n;` -/
def push (k : Int) : Stmt := seqs [malloc 1 0 (lit 1), store 1 (lit 0) 0 (lit k), put 1 (lit 0) 1 0, move 0 1]
def listProg : Stmt := seqs [nul 0, push 1, push 2, push 3,
  set 3 (lit 0), borrow 2 0 (lit 0),
  loop (nz 2) (seqs [load 4 2 (lit 0) 0, set 3 (bin .add (var 3) (var 4)), load 2 2 (lit 0) 1]),
  move 1 0, take 0 1 (lit 0) 1, load 4 1 (lit 0) 0, free 1,
  loop (nz 0) (seqs [move 1 0, take 0 1 (lit 0) 1, free 1])]
theorem idiom1_list : ok Rules.full P0 lD listProg = true ∧ (run P0 listProg).isUB = false := by decide

/-- The usual C pop, `n = head; head = n->next; free(n);`, copies an owned
field without nulling it. In this model that is `load` (a borrow), and
`head` cannot take ownership from a borrow. -/
def popC : Stmt := seqs [nul 0, push 1, move 1 0, load 2 1 (lit 0) 1, move 0 2, free 1]
theorem idiom1_c_pop_rejected : ok Rules.full P0 lD popC = false := by decide

/-! ### 2. Doubly linked list: no pointer form; indices into one owned array -/

def dD : Decls := [(0, .own 2), (1, .int), (2, .int), (3, .int)]
def dlist : Stmt := seqs [malloc 0 2 (lit 3),
  store 0 (lit 0) 2 (lit 1), store 0 (lit 1) 1 (lit 0), store 0 (lit 1) 2 (lit 2), store 0 (lit 2) 1 (lit 1),
  store 0 (lit 2) 2 (lit (-1)),
  set 1 (lit 0), set 3 (lit 0),
  loop (bin .lt (lit (-1)) (var 1)) (seqs [set 3 (bin .add (var 3) (ld 0 (var 1) 0)), set 1 (ld 0 (var 1) 2)]),
  free 0]
theorem idiom2_indices : ok Rules.full P0 dD dlist = true ∧ (run P0 dlist).isUB = false := by decide

/-! ### 3. Growable array: hold an index across the push, re-borrow after -/

def gD : Decls := [(0, .own 1), (1, .ref 1), (2, .int), (3, .int)]
def grow : Stmt := seqs [malloc 0 1 (lit 2), set 2 (lit 1), realloc 0 1 (lit 4),
  borrow 1 0 (var 2), load 3 1 (lit 0) 0, free 0]
theorem idiom3_index : ok Rules.full P0 gD grow = true ∧ (run P0 grow).isUB = false := by decide
theorem idiom3_pointer_rejected : ok Rules.full P0 cReallocD cRealloc = false := by decide

/-! ### 4. Arena: many borrows of one owner; reset ends them all -/

def aD : Decls := [(0, .own 1), (1, .ref 1), (2, .ref 1), (3, .int)]
def arenaUse : List Stmt := [malloc 0 1 (lit 8), borrow 1 0 (lit 0), borrow 2 0 (lit 1),
  store 1 (lit 0) 0 (lit 5), store 2 (lit 0) 0 (lit 7), load 3 1 (lit 0) 0, realloc 0 1 (lit 0)]
def arena : Stmt := seqs (arenaUse ++ [free 0])
def arenaStale : Stmt := seqs (arenaUse ++ [load 3 2 (lit 0) 0, free 0])
theorem idiom4_arena : ok Rules.full P0 aD arena = true ∧ (run P0 arena).isUB = false ∧
    ok Rules.full P0 aD arenaStale = false := by decide

/-! ### 5. A parser holding `const char *input`, as locals or a parameter -/

def parse : Fn := ⟨[0], [(0, .ref 1), (1, .int), (2, .int)],
  seqs [set 1 (lit 0), set 2 (lit 0),
    loop (bin .lt (var 1) (lit 4)) (seqs [set 2 (bin .add (var 2) (ld 0 (var 1) 0)), set 1 (bin .add (var 1) (lit 1))])],
  2, none⟩
def PParse : Prog := ⟨S, [parse]⟩
def pD : Decls := [(0, .own 1), (1, .ref 1), (2, .int)]
def parseProg : Stmt := seqs [malloc 0 1 (lit 4), store 0 (lit 2) 0 (lit 9), borrow 1 0 (lit 0), call 2 0 [1], free 0]
theorem idiom5_parser : ok Rules.full PParse pD parseProg = true ∧ (run PParse parseProg).isUB = false := by decide

/-! ### 6. Out-parameter as an owned return; callback with a context pointer

`void make(T **out)` becomes `_Owned T *make(void)`. `each(list, ctx)` calls
`cb(ctx, v)` for every node; both receive borrows for the call only. -/

def make : Fn := ⟨[], [(0, .own 1)], malloc 0 1 (lit 1), 0, none⟩
def cb : Fn := ⟨[0, 1], [(0, .ref 1), (1, .int), (2, .int)],
  seqs [store 0 (lit 0) 0 (bin .add (ld 0 (lit 0) 0) (var 1)), set 2 (lit 0)], 2, none⟩
def each : Fn := ⟨[0, 1], [(0, .ref 0), (1, .ref 1), (3, .int), (4, .int), (5, .int)],
  seqs [loop (nz 0) (seqs [load 3 0 (lit 0) 0, call 4 1 [1, 3], load 0 0 (lit 0) 1]), set 5 (lit 0)],
  5, none⟩
def P6 : Prog := ⟨S, [make, cb, each]⟩
def eD : Decls := [(0, .own 0), (1, .own 0), (5, .own 1), (6, .ref 0), (7, .ref 1), (8, .int)]
def eachProg : Stmt := seqs [call 5 0 [], nul 0, push 4, push 5, borrow 6 0 (lit 0), borrow 7 5 (lit 0),
  call 8 2 [6, 7], free 5,
  loop (nz 0) (seqs [move 1 0, take 0 1 (lit 0) 1, free 1])]
theorem idiom6_out_and_callback : ok Rules.full P6 eD eachProg = true ∧ (run P6 eachProg).isUB = false := by decide

/-! ### `lifetimebound`: `node *second(node *l __lifetimebound) { return l->next; }` -/

def second : Fn := ⟨[0], [(0, .ref 0), (1, .ref 0)], load 1 0 (lit 0) 1, 1, some 0⟩
def PSec : Prog := ⟨S, [second]⟩
def sD : Decls := [(0, .own 0), (1, .own 0), (2, .ref 0), (3, .ref 0), (4, .int)]
def secUse : List Stmt := [nul 0, push 1, push 2, borrow 2 0 (lit 0), call 3 0 [2], load 4 3 (lit 0) 0]
def freeAll : Stmt := loop (nz 0) (seqs [move 1 0, take 0 1 (lit 0) 1, free 1])
def secProg : Stmt := seqs (secUse ++ [freeAll])
def secStale : Stmt := seqs (secUse ++ [freeAll, load 4 3 (lit 0) 0])
theorem lifetimebound_ok : ok Rules.full PSec sD secProg = true ∧ (run PSec secProg).isUB = false ∧
    ok Rules.full PSec sD secStale = false := by decide

end SafeC.Own
