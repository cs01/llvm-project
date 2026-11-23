-- Ownership and lifetimes for Safe C: owned and borrowed pointers, free,
-- realloc, and per-function checking. Syntax, semantics and the checker.
import SafeC.Defs

namespace SafeC.Own

open SafeC (Op wrap INT_MIN INT_MAX)

/-! ## Types

Heap fields hold integers or owned pointers, never borrows: a borrow stored in
the heap could outlive its owner, and this model has no lifetime parameters on
struct types to prevent that (see OWNERSHIP.md, idiom 5). -/

inductive FTy where
  | int
  | own (s : Nat)
  deriving DecidableEq, Repr

/-- Variable types. `own s` is `_Owned struct s *` (nullable), `ref s` is a
plain `struct s *`: a borrow. -/
inductive VTy where
  | int
  | own (s : Nat)
  | ref (s : Nat)
  deriving DecidableEq, Repr

/-! ## Values and memory -/

inductive V where
  | int (n : Int)
  | ptr (b : Nat) (off : Int)
  | null
  | uninit
  deriving DecidableEq, Repr

/-- A heap block: an array of struct elements, each a list of field values.
Freed blocks stay in the list with `live = false`, so ids are never reused and
a stale pointer is detectable as use after free. -/
structure Blk where
  live : Bool
  /-- Struct id at allocation. Ghost: the semantics never reads it. -/
  sid : Nat
  cells : List (List V)

abbrev Mem := List Blk
abbrev Env := Nat → V

def Env.set (ρ : Env) (x : Nat) (v : V) : Env := fun y => if y = x then v else ρ y

inductive UB where
  | useAfterFree
  | doubleFree
  | other
  deriving DecidableEq, Repr

inductive Res (α : Type) where
  | ok (a : α)
  | trap
  | ub (k : UB)

def Res.bind {α β : Type} : Res α → (α → Res β) → Res β
  | .ok a, f => f a
  | .trap, _ => .trap
  | .ub k, _ => .ub k

inductive Out where
  | ok (ρ : Env) (μ : Mem)
  | trap
  | ub (k : UB)
  | outOfFuel

def Res.thenO {α : Type} (r : Res α) (f : α → Out) : Out :=
  match r with
  | .ok a => f a
  | .trap => .trap
  | .ub k => .ub k

def Out.then (o : Out) (f : Env → Mem → Out) : Out :=
  match o with
  | .ok ρ μ => f ρ μ
  | o => o

def Out.isUB : Out → Bool
  | .ub _ => true
  | _ => false

def Out.ubKind : Out → Option UB
  | .ub k => some k
  | _ => none

/-! ## Syntax -/

inductive Expr where
  | lit (n : Int)
  | var (x : Nat)
  | bin (op : Op) (a b : Expr)
  /-- `y[i].f`, an integer field. -/
  | ld (y : Nat) (i : Expr) (f : Nat)
  /-- `y != NULL`. -/
  | nz (y : Nat)
  deriving Repr

inductive Stmt where
  | skip
  | seq (a b : Stmt)
  /-- `x = e`, integer. -/
  | set (x : Nat) (e : Expr)
  /-- `x = NULL`. -/
  | nul (x : Nat)
  /-- `x = y + i` where `x` is a plain pointer: a borrow of `y`'s storage. -/
  | borrow (x y : Nat) (i : Expr)
  /-- `x = y` between owned pointers: a move. -/
  | move (x y : Nat)
  /-- `x = y[i].f`: copies an integer field, or borrows an owned field. -/
  | load (x y : Nat) (i : Expr) (f : Nat)
  /-- `y[i].f = e`, integer field. -/
  | store (y : Nat) (i : Expr) (f : Nat) (e : Expr)
  /-- `y[i].f = z` into an owned field; `z` is moved. -/
  | put (y : Nat) (i : Expr) (f : Nat) (z : Nat)
  /-- `x = y[i].f; y[i].f = NULL;` out of an owned field. -/
  | take (x y : Nat) (i : Expr) (f : Nat)
  /-- `x = calloc(n, sizeof(struct s))`. -/
  | malloc (x : Nat) (s : Nat) (n : Expr)
  | free (x : Nat)
  /-- `x = realloc(x, n * sizeof(struct s))`, new elements zeroed. -/
  | realloc (x : Nat) (s : Nat) (n : Expr)
  | ifz (c : Expr) (a b : Stmt)
  | ifNull (x : Nat) (a b : Stmt)
  | loop (c : Expr) (body : Stmt)
  /-- `x = f(args)`. -/
  | call (x : Nat) (f : Nat) (args : List Nat)
  deriving Repr

abbrev Decls := List (Nat × VTy)

/-- A function. Its body runs in a fresh frame holding only the parameters. -/
structure Fn where
  params : List Nat
  decls : Decls
  body : Stmt
  ret : Nat
  /-- `lifetimebound`: the returned borrow comes from this parameter. -/
  bound : Option Nat

structure Prog where
  structs : List (List FTy)
  fns : List Fn

/-! ## Semantics -/

def fieldTy (P : Prog) (s f : Nat) : Option FTy := (P.structs[s]?).bind (·[f]?)

def zeroOf : FTy → V
  | .int => .int 0
  | .own _ => .null

def zeroElem (P : Prog) (s : Nat) : List V := ((P.structs[s]?).getD []).map zeroOf

def getc (μ : Mem) (b i j : Nat) : Option V :=
  match μ[b]? with
  | some blk => match blk.cells[i]? with
    | some el => el[j]?
    | none => none
  | none => none

def setc (μ : Mem) (b i j : Nat) (v : V) : Mem :=
  match μ[b]? with
  | some blk => match blk.cells[i]? with
    | some el => μ.set b { blk with cells := blk.cells.set i (el.set j v) }
    | none => μ
  | none => μ

/-- Element `off + i` of the block `v` points into. A freed block is use after
free; out of bounds traps (the checked access); null traps. -/
def elemAt (μ : Mem) (v : V) (i : Int) : Res (Nat × Nat) :=
  match v with
  | .ptr b off =>
    match μ[b]? with
    | some blk =>
      if blk.live = true then
        (if 0 ≤ off + i ∧ off + i < blk.cells.length then .ok (b, (off + i).toNat) else .trap)
      else .ub .useAfterFree
    | none => .ub .other
  | .null => .trap
  | _ => .ub .other

def rdF (μ : Mem) (b k f : Nat) : Res V :=
  match getc μ b k f with
  | some v => .ok v
  | none => .ub .other

def wrF (μ : Mem) (b k f : Nat) (v : V) : Res Mem :=
  match getc μ b k f with
  | some _ => .ok (setc μ b k f v)
  | none => .ub .other

def arithV (op : Op) (x y : Int) : Res V :=
  match op with
  | .add => .ok (.int (wrap (x + y)))
  | .sub => .ok (.int (wrap (x - y)))
  | .mul => .ok (.int (wrap (x * y)))
  | .div => if y = 0 ∨ (x = INT_MIN ∧ y = -1) then .trap else .ok (.int (wrap (x.tdiv y)))
  | .lt => .ok (.int (if x < y then 1 else 0))

def evalE (ρ : Env) (μ : Mem) : Expr → Res V
  | .lit n => .ok (.int n)
  | .var x => if ρ x = .uninit then .ub .other else .ok (ρ x)
  | .bin op a b =>
    (evalE ρ μ a).bind fun va => (evalE ρ μ b).bind fun vb =>
      match va, vb with
      | .int x, .int y => arithV op x y
      | _, _ => .ub .other
  | .ld y i f =>
    (evalE ρ μ i).bind fun vi =>
      match vi with
      | .int n => (elemAt μ (ρ y) n).bind fun a => rdF μ a.1 a.2 f
      | _ => .ub .other
  | .nz y =>
    match ρ y with
    | .null => .ok (.int 0)
    | .ptr _ _ => .ok (.int 1)
    | _ => .ub .other

/-- Address of field `f` of element `i` of `y`. -/
def fieldAt (ρ : Env) (μ : Mem) (y : Nat) (i : Expr) : Res (Nat × Nat) :=
  (evalE ρ μ i).bind fun vi =>
    match vi with
    | .int n => elemAt μ (ρ y) n
    | _ => .ub .other

def freshBlk (P : Prog) (s : Nat) (m : Nat) : Blk := ⟨true, s, List.replicate m (zeroElem P s)⟩

def kill (μ : Mem) (b : Nat) (blk : Blk) : Mem := μ.set b { blk with live := false }

def bindArgs (ρ : Env) (params args : List Nat) : Env :=
  fun v => match (params.zip args).lookup v with
    | some a => ρ a
    | none => .uninit

/-- Fuel-bounded big-step execution; `outOfFuel` stands for divergence. -/
def exec (P : Prog) : Nat → Env → Mem → Stmt → Out
  | 0, _, _, _ => .outOfFuel
  | n + 1, ρ, μ, s =>
    match s with
    | .skip => .ok ρ μ
    | .seq a b => (exec P n ρ μ a).then fun ρ' μ' => exec P n ρ' μ' b
    | .set x e => (evalE ρ μ e).thenO fun v => .ok (ρ.set x v) μ
    | .nul x => .ok (ρ.set x .null) μ
    | .borrow x y i => (evalE ρ μ i).thenO fun vi =>
      match ρ y, vi with
      | .ptr b off, .int k => .ok (ρ.set x (.ptr b (off + k))) μ
      | .null, .int _ => .ok (ρ.set x .null) μ
      | _, _ => .ub .other
    | .move x y => if ρ y = .uninit then .ub .other else .ok (ρ.set x (ρ y)) μ
    | .load x y i f => (fieldAt ρ μ y i).thenO fun a =>
      (rdF μ a.1 a.2 f).thenO fun v => .ok (ρ.set x v) μ
    | .store y i f e => (fieldAt ρ μ y i).thenO fun a =>
      (evalE ρ μ e).thenO fun v => (wrF μ a.1 a.2 f v).thenO fun μ' => .ok ρ μ'
    | .put y i f z => (fieldAt ρ μ y i).thenO fun a =>
      if ρ z = .uninit then .ub .other else (wrF μ a.1 a.2 f (ρ z)).thenO fun μ' => .ok ρ μ'
    | .take x y i f => (fieldAt ρ μ y i).thenO fun a =>
      (rdF μ a.1 a.2 f).thenO fun v => (wrF μ a.1 a.2 f .null).thenO fun μ' => .ok (ρ.set x v) μ'
    | .malloc x s e => (evalE ρ μ e).thenO fun vn =>
      match vn with
      | .int m => if m < 0 then .trap else .ok (ρ.set x (.ptr μ.length 0)) (μ ++ [freshBlk P s m.toNat])
      | _ => .ub .other
    | .free x =>
      match ρ x with
      | .null => .ok ρ μ
      | .ptr b off =>
        match μ[b]? with
        | some blk =>
          if blk.live = false then .ub .doubleFree
          else if off = 0 then .ok ρ (kill μ b blk) else .ub .other
        | none => .ub .other
      | _ => .ub .other
    | .realloc x s e => (evalE ρ μ e).thenO fun vn =>
      match vn with
      | .int m =>
        if m < 0 then .trap else
        match ρ x with
        | .null => .ok (ρ.set x (.ptr μ.length 0)) (μ ++ [freshBlk P s m.toNat])
        | .ptr b off =>
          match μ[b]? with
          | some blk =>
            if blk.live = false then .ub .useAfterFree
            else if off = 0 then
              let cells := blk.cells.take m.toNat ++ List.replicate (m.toNat - blk.cells.length) (zeroElem P s)
              .ok (ρ.set x (.ptr μ.length 0)) (kill μ b blk ++ [⟨true, s, cells⟩])
            else .ub .other
          | none => .ub .other
        | _ => .ub .other
      | _ => .ub .other
    | .ifz c a b => (evalE ρ μ c).thenO fun v =>
      match v with
      | .int z => if z ≠ 0 then exec P n ρ μ a else exec P n ρ μ b
      | _ => .ub .other
    | .ifNull x a b =>
      match ρ x with
      | .null => exec P n ρ μ a
      | .ptr _ _ => exec P n ρ μ b
      | _ => .ub .other
    | .loop c body => (evalE ρ μ c).thenO fun v =>
      match v with
      | .int z =>
        if z = 0 then .ok ρ μ else
        (exec P n ρ μ body).then fun ρ' μ' => exec P n ρ' μ' (.loop c body)
      | _ => .ub .other
    | .call x f args =>
      match P.fns[f]? with
      | none => .ub .other
      | some fn =>
        if args.any (fun a => decide (ρ a = .uninit)) then .ub .other else
        match exec P n (bindArgs ρ fn.params args) μ fn.body with
        | .ok ρ' μ' => if ρ' fn.ret = .uninit then .ub .other else .ok (ρ.set x (ρ' fn.ret)) μ'
        | .trap => .trap
        | .ub k => .ub k
        | .outOfFuel => .outOfFuel

/-! ## Rule switches

Each field is one ownership rule. `Rules.full` has them all; OwnHoles.lean
turns each off in turn and shows a well-typed program that reaches UB. -/

structure Rules where
  /-- A moved-from owner is dead. -/
  moveKills : Bool
  /-- `free(x)` ends every loan of `x`. -/
  freeKills : Bool
  /-- `take`/`put` through an owner end its loans. -/
  takeKills : Bool
  /-- `realloc(x)` ends every loan of `x`. -/
  reallocKills : Bool
  /-- `free`, `take` and `put` need an owned pointer, not a borrow. -/
  ownOpsNeedOwner : Bool
  /-- Passing an owner to an owned parameter moves it. -/
  argsConsume : Bool
  /-- Borrow arguments must still be live after the owned arguments moved. -/
  argsAfterMoves : Bool
  /-- A returned borrow must come from the `lifetimebound` parameter, and the
  caller ties the result to that argument's loan. -/
  retBound : Bool
  deriving Repr

def Rules.full : Rules :=
  { moveKills := true, freeKills := true, takeKills := true, reallocKills := true,
    ownOpsNeedOwner := true, argsConsume := true, argsAfterMoves := true, retBound := true }

/-! ## The checker

Flow-sensitive and per function. `live` is the set of usable variables: an
initialized integer, an owner that still owns, or a borrow whose loan is
still live. `roots` records which loan each borrow depends on. -/

inductive Root where
  /-- A loan of the local owner `x`. -/
  | loc (x : Nat)
  /-- A loan the caller holds; borrowed through parameter `x` for the call. -/
  | ext (x : Nat)
  deriving DecidableEq, Repr

structure Ctx where
  live : List Nat
  roots : List (Nat × Root)
  deriving Repr

def Ctx.empty : Ctx := ⟨[], []⟩

/-- End every loan rooted at `r`. -/
def Ctx.kill (Γ : Ctx) (r : Root) : Ctx :=
  { Γ with live := Γ.live.filter fun y => decide (Γ.roots.lookup y ≠ some r) }
def Ctx.add (Γ : Ctx) (x : Nat) : Ctx := { Γ with live := x :: Γ.live }
def Ctx.drop (Γ : Ctx) (x : Nat) : Ctx := { Γ with live := Γ.live.filter fun y => decide (y ≠ x) }
def Ctx.addRef (Γ : Ctx) (x : Nat) (r : Root) : Ctx := ⟨x :: Γ.live, (x, r) :: Γ.roots⟩
/-- Assign a new value to owner `x`: its old loans end. -/
def Ctx.assignOwn (Γ : Ctx) (x : Nat) : Ctx := (Γ.kill (.loc x)).add x

def isRef (D : Decls) (x : Nat) : Bool :=
  match D.lookup x with
  | some (.ref _) => true
  | _ => false

def isOwn (D : Decls) (x : Nat) : Bool :=
  match D.lookup x with
  | some (.own _) => true
  | _ => false

def ptrS (D : Decls) (y : Nat) : Option Nat :=
  match D.lookup y with
  | some (.own s) => some s
  | some (.ref s) => some s
  | _ => none

/-- The loan a borrow taken from `y` depends on. -/
def rootOf (D : Decls) (Γ : Ctx) (y : Nat) : Option Root :=
  match D.lookup y with
  | some (.own _) => some (.loc y)
  | some (.ref _) => Γ.roots.lookup y
  | _ => none

def ownerOK (R : Rules) (D : Decls) (y s : Nat) : Bool :=
  decide (D.lookup y = some (.own s)) || (!R.ownOpsNeedOwner && decide (D.lookup y = some (.ref s)))

def killRoot (b : Bool) (D : Decls) (Γ : Ctx) (y : Nat) : Ctx :=
  if b then (match rootOf D Γ y with | some r => Γ.kill r | none => Γ) else Γ

def consume (R : Rules) (Γ : Ctx) (z : Nat) : Ctx :=
  if R.moveKills then (Γ.kill (.loc z)).drop z else Γ

def meet (D : Decls) (F G : Ctx) : Ctx :=
  ⟨F.live.filter (fun x => decide (x ∈ G.live) &&
      (if isRef D x then decide (F.roots.lookup x = G.roots.lookup x) else true)), F.roots⟩

/-- `G` keeps everything `F` knows. -/
def sub (D : Decls) (F G : Ctx) : Bool :=
  F.live.all fun x => decide (x ∈ G.live) &&
    (if isRef D x then decide (G.roots.lookup x = F.roots.lookup x) else true)

def chkE (P : Prog) (D : Decls) (Γ : Ctx) : Expr → Bool
  | .lit _ => true
  | .var x => decide (x ∈ Γ.live) && decide (D.lookup x = some .int)
  | .bin _ a b => chkE P D Γ a && chkE P D Γ b
  | .ld y i f => decide (y ∈ Γ.live) && chkE P D Γ i &&
      (match ptrS D y with
       | some s => decide (fieldTy P s f = some .int)
       | none => false)
  | .nz y => decide (y ∈ Γ.live) && (ptrS D y).isSome

/-- Owned arguments, left to right: each must be live, and (with
`argsConsume`) is moved into the callee. -/
def consumeArgs (R : Rules) (D : Decls) : Ctx → List Nat → Option Ctx
  | Γ, [] => some Γ
  | Γ, a :: as =>
    if isOwn D a then
      (if a ∈ Γ.live then consumeArgs R D (if R.argsConsume then (Γ.kill (.loc a)).drop a else Γ) as
       else none)
    else consumeArgs R D Γ as

def chkS (R : Rules) (P : Prog) (D : Decls) : Ctx → Stmt → Option Ctx
  | Γ, .skip => some Γ
  | Γ, .seq a b => (chkS R P D Γ a).bind fun Γ1 => chkS R P D Γ1 b
  | Γ, .set x e => if D.lookup x = some .int ∧ chkE P D Γ e = true then some (Γ.add x) else none
  | Γ, .nul x =>
    match D.lookup x with
    | some (.own _) => some (Γ.assignOwn x)
    | some (.ref _) => some (Γ.add x)
    | _ => none
  | Γ, .borrow x y i =>
    match D.lookup x, ptrS D y, rootOf D Γ y with
    | some (.ref s), some s', some r =>
      if s = s' ∧ y ∈ Γ.live ∧ chkE P D Γ i = true then some (Γ.addRef x r) else none
    | _, _, _ => none
  | Γ, .move x y =>
    match D.lookup x, D.lookup y with
    | some (.own s), some (.own s') =>
      if s = s' ∧ x ≠ y ∧ y ∈ Γ.live then some ((consume R Γ y).assignOwn x) else none
    | _, _ => none
  | Γ, .load x y i f =>
    match ptrS D y with
    | none => none
    | some s =>
      if y ∈ Γ.live ∧ chkE P D Γ i = true then
        match fieldTy P s f, D.lookup x with
        | some .int, some .int => some (Γ.add x)
        | some (.own s'), some (.ref s'') =>
          match rootOf D Γ y with
          | some r => if s' = s'' then some (Γ.addRef x r) else none
          | none => none
        | _, _ => none
      else none
  | Γ, .store y i f e =>
    match ptrS D y with
    | some s =>
      if y ∈ Γ.live ∧ chkE P D Γ i = true ∧ chkE P D Γ e = true ∧ fieldTy P s f = some .int
      then some Γ else none
    | none => none
  | Γ, .put y i f z =>
    match ptrS D y, D.lookup z with
    | some s, some (.own s') =>
      if ownerOK R D y s = true ∧ y ∈ Γ.live ∧ z ∈ Γ.live ∧ z ≠ y ∧ chkE P D Γ i = true ∧
          fieldTy P s f = some (.own s')
      then some (consume R (killRoot R.takeKills D Γ y) z) else none
    | _, _ => none
  | Γ, .take x y i f =>
    match ptrS D y, D.lookup x with
    | some s, some (.own s') =>
      if ownerOK R D y s = true ∧ y ∈ Γ.live ∧ x ≠ y ∧ chkE P D Γ i = true ∧
          fieldTy P s f = some (.own s')
      then some ((killRoot R.takeKills D Γ y).assignOwn x) else none
    | _, _ => none
  | Γ, .malloc x s n =>
    if D.lookup x = some (.own s) ∧ s < P.structs.length ∧ chkE P D Γ n = true
    then some (Γ.assignOwn x) else none
  | Γ, .free x =>
    match ptrS D x with
    | some s => if ownerOK R D x s = true ∧ x ∈ Γ.live then some ((killRoot R.freeKills D Γ x).drop x) else none
    | none => none
  | Γ, .realloc x s n =>
    if D.lookup x = some (.own s) ∧ x ∈ Γ.live ∧ chkE P D Γ n = true
    then some (if R.reallocKills then Γ.kill (.loc x) else Γ) else none
  | Γ, .ifz c a b =>
    if chkE P D Γ c = true then
      (chkS R P D Γ a).bind fun Fa => (chkS R P D Γ b).bind fun Fb => some (meet D Fa Fb)
    else none
  | Γ, .ifNull x a b =>
    if x ∈ Γ.live ∧ (ptrS D x).isSome = true then
      (chkS R P D Γ a).bind fun Fa => (chkS R P D Γ b).bind fun Fb => some (meet D Fa Fb)
    else none
  /- One round of fixpoint iteration: the loop invariant is what the entry and
  one pass of the body agree on, and a second pass must keep it. -/
  | Γ, .loop c body =>
    match chkS R P D Γ body with
    | some Γ1 =>
      let Γi := meet D Γ Γ1
      if chkE P D Γi c = true then
        match chkS R P D Γi body with
        | some Γ2 => if sub D Γi Γ2 = true then some Γi else none
        | none => none
      else none
    | none => none
  | Γ, .call x f args =>
    match P.fns[f]? with
    | none => none
    | some fn =>
      if args.length = fn.params.length ∧
          ((fn.params.zip args).all fun pa =>
            decide (D.lookup pa.2 = fn.decls.lookup pa.1 ∧ (D.lookup pa.2).isSome = true)) = true then
        match consumeArgs R D Γ args with
        | none => none
        | some Γm =>
          let Γc := if R.argsAfterMoves then Γm else Γ
          if (args.all fun a => isOwn D a || decide (a ∈ Γc.live)) = true then
            match fn.decls.lookup fn.ret, D.lookup x with
            | some .int, some .int => some (Γm.add x)
            | some (.own s), some (.own s') => if s = s' then some (Γm.assignOwn x) else none
            | some (.ref s), some (.ref s') =>
              if s = s' then
                (if R.retBound then
                  match fn.bound with
                  | some p =>
                    match (fn.params.zip args).lookup p with
                    | some a =>
                      match Γ.roots.lookup a with
                      | some r => some (Γm.addRef x r)
                      | none => none
                    | none => none
                  | none => none
                else some (Γm.addRef x (.ext x)))
              else none
            | _, _ => none
          else none
      else none

/-- The callee's entry context: its parameters, borrows rooted at the caller. -/
def Fn.init (fn : Fn) : Ctx :=
  ⟨fn.params, fn.params.filterMap fun p => if isRef fn.decls p then some (p, .ext p) else none⟩

def fnOK (R : Rules) (P : Prog) (fn : Fn) : Bool :=
  match chkS R P fn.decls fn.init fn.body with
  | none => false
  | some Γf =>
    decide fn.params.Nodup && decide (fn.ret ∈ Γf.live) &&
    (match fn.decls.lookup fn.ret with
     | some (.ref _) =>
       if R.retBound then
         (match fn.bound with
          | some p => decide (Γf.roots.lookup fn.ret = some (.ext p)) && decide (p ∈ fn.params) &&
              isRef fn.decls p
          | none => false)
       else true
     | some _ => true
     | none => false)

def Prog.ok (R : Rules) (P : Prog) : Bool := P.fns.all (fnOK R P)

def run (P : Prog) (s : Stmt) (fuel : Nat := 400) : Out := exec P fuel (fun _ => .uninit) [] s

end SafeC.Own
