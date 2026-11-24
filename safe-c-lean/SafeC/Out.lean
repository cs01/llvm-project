-- Out arguments and heap view arrays for Safe C: syntax, semantics with use
-- of freed memory, double free and reads of dead locals as UB, the rule
-- switches and the checker. Functions are two-sided: a call is checked
-- against the callee's signature only, and each body is checked on its own.
namespace SafeC.Out

/-! ## State

Memory is a list of one-cell blocks. A variable's storage is a block too
(variable `v` of the frame at `fp` is block `fp + v`), so `&x` and a pointer
into the heap are the same kind of value. -/

inductive Val where
  | null
  | ptr (b : Nat)
  deriving DecidableEq, Repr

/-- Types of variables. `own` stands for `int *_Owned p` and for a struct
whose field `p` that is. -/
inductive Ty where
  /-- `int x`: only its address matters. -/
  | int
  /-- `int *x`, a plain borrow. -/
  | bor
  | own
  /-- `struct tok *_Owned t`: owns a heap view, whose cell holds a borrow. -/
  | vown
  /-- `struct tok *q`, only ever set to `t`. -/
  | alias (t : Nat)
  /-- Parameter `int **out` or `struct tok *out`: points at a cell of the
  caller that holds a borrow. -/
  | out
  /-- Parameter `struct own *s`. -/
  | sh
  /-- Parameter `struct own *_Exclusive s`. -/
  | ex
  deriving DecidableEq, Repr

/-- What a block is. Ghost: the proof reads it, and it names the UB. -/
inductive Kind where
  | lint | bvar | ocell | vcell | pcell
  /-- Heap int, heap view. -/
  | hint | hview
  deriving DecidableEq, Repr

def Ty.kind : Ty → Kind
  | .int => .lint
  | .bor => .bvar
  | .own => .ocell
  | .vown => .vcell
  | .alias _ | .out | .sh | .ex => .pcell

structure St where
  alive : Nat → Bool
  cont : Nat → Val
  kind : Nat → Kind
  next : Nat

def upd {α : Type} (f : Nat → α) (x : Nat) (v : α) : Nat → α :=
  fun y => if y = x then v else f y

def St.set (st : St) (b : Nat) (v : Val) : St := { st with cont := upd st.cont b v }
def St.kill (st : St) (b : Nat) : St := { st with alive := upd st.alive b false }

/-- A fresh heap block. -/
def St.grow (st : St) (k : Kind) : St :=
  { alive := upd st.alive st.next true, cont := upd st.cont st.next .null,
    kind := upd st.kind st.next k, next := st.next + 1 }

/-- Block 0 is a global owner (`static struct own g`); block 1 is a global
pointer to views, which rule `viewNoField` keeps null. -/
def St.init : St :=
  ⟨fun b => b < 2, fun _ => .null, fun b => if b = 0 then .ocell else .vcell, 2⟩

inductive UB where
  | useAfterFree
  | doubleFree
  | deadLocal
  deriving DecidableEq, Repr

inductive Out where
  | ok (st : St)
  | trap
  | ub (k : UB)
  | outOfFuel

def Out.isUB : Out → Bool
  | .ub _ => true
  | _ => false

def Out.then (o : Out) (f : St → Out) : Out :=
  match o with
  | .ok st => f st
  | o => o

/-! ## Syntax -/

inductive Stmt where
  | skip
  | seq (a b : Stmt)
  /-- `if (c) a else b`, for a condition the checker does not look at. -/
  | branch (c : Bool) (a b : Stmt)
  /-- `x = NULL`. -/
  | null (x : Nat)
  /-- `x = &y` for `int y`. -/
  | addr (x y : Nat)
  /-- `x = y`. -/
  | copy (x y : Nat)
  /-- `*x`. -/
  | deref (x : Nat)
  /-- `v = malloc(n)` for an owner, `v->p = malloc(n)` for an `ex` parameter. -/
  | new (v : Nat)
  /-- `free(_Take(v))`, `free(_Take(v->p))`. -/
  | free (v : Nat)
  /-- `x = v.p`, `x = v->p`. -/
  | get (x v : Nat)
  /-- `g.p = malloc(n)`, `free(_Take(g.p))`, `x = g.p`. -/
  | newG
  | freeG
  | getG (x : Nat)
  /-- `t = calloc(1, sizeof(struct tok))`. -/
  | newv (t : Nat)
  /-- `q = t`, for the `t` in `q`'s type. -/
  | mkalias (q : Nat)
  /-- `x = *q` (`x = q[0].text`). -/
  | load (x q : Nat)
  /-- `*q = x` (`q[0].text = x`). -/
  | store (q x : Nat)
  /-- `gtoks = _Take(t)`, `t = _Take(gtoks)`. -/
  | gput (t : Nat)
  | gget (t : Nat)
  /-- `ret = f(args)`. An argument is a variable; what is passed (its value
  or its address) follows from its type and the parameter's. -/
  | call (f : Nat) (args : List Nat) (ret : Option Nat)
  deriving Repr

structure Fn where
  /-- Each parameter's type, and whether it is `_Noescape`. -/
  params : List (Ty × Bool)
  locals : List Ty
  body : Stmt
  /-- The `int *` variable whose final value is returned. -/
  ret : Option Nat

def Fn.tys (F : Fn) : List Ty := F.params.map (·.1) ++ F.locals

/-! ## Semantics -/

/-- Reach a pointer's target. -/
def St.reach (st : St) (v : Val) (f : Nat → Out) : Out :=
  match v with
  | .null => .trap
  | .ptr b =>
    if st.alive b then f b
    else if st.kind b = .hint ∨ st.kind b = .hview then .ub .useAfterFree else .ub .deadLocal

/-- `free(_Take(cell))`. -/
def freeCell (st : St) (c : Nat) : Out :=
  match st.cont c with
  | .null => .ok st
  | .ptr b => if st.alive b then .ok ((st.kill b).set c .null) else .ub .doubleFree

/-- The cell that holds what `v` owns: `v`'s own storage, or what the
parameter `v` points to. -/
def ownCell (D : List Ty) (fp : Nat) (st : St) (v : Nat) (f : Nat → Out) : Out :=
  match D[v]? with
  | some .own | some .vown => f (fp + v)
  | _ => st.reach (st.cont (fp + v)) f

/-- What is passed for a parameter of type `pt` when the argument is `v`. -/
def argVal (D : List Ty) (fp : Nat) (st : St) (pt : Ty) (v : Nat) : Val :=
  match pt, D[v]? with
  | .out, some .bor | .sh, some .own | .ex, some .own => .ptr (fp + v)
  | _, _ => st.cont (fp + v)

/-- Enter a frame of `W` variables at `st.next`, the first ones holding `vals`. -/
def enter (st : St) (D : List Ty) (vals : List Val) : St :=
  { alive := fun b => if st.next ≤ b ∧ b < st.next + D.length then true else st.alive b
    cont := fun b => if st.next ≤ b ∧ b < st.next + D.length
      then (vals[b - st.next]?).getD .null else st.cont b
    kind := fun b => if st.next ≤ b ∧ b < st.next + D.length
      then ((D[b - st.next]?).getD .int).kind else st.kind b
    next := st.next + D.length }

/-- Leave it: its variables die. -/
def leave (st : St) (fp W : Nat) : St :=
  { st with alive := fun b => if fp ≤ b ∧ b < fp + W then false else st.alive b }

def exec (P : List Fn) : Nat → List Ty → Nat → St → Stmt → Out
  | 0, _, _, _, _ => .outOfFuel
  | n + 1, D, fp, st, s =>
    match s with
    | .skip => .ok st
    | .seq a b => (exec P n D fp st a).then fun st' => exec P n D fp st' b
    | .branch c a b => if c then exec P n D fp st a else exec P n D fp st b
    | .null x => .ok (st.set (fp + x) .null)
    | .addr x y => .ok (st.set (fp + x) (.ptr (fp + y)))
    | .copy x y => .ok (st.set (fp + x) (st.cont (fp + y)))
    | .deref x => st.reach (st.cont (fp + x)) fun _ => .ok st
    | .new v => ownCell D fp st v fun c => .ok ((st.grow .hint).set c (.ptr st.next))
    | .free v => ownCell D fp st v fun c => freeCell st c
    | .get x v => ownCell D fp st v fun c => .ok (st.set (fp + x) (st.cont c))
    | .newG => .ok ((st.grow .hint).set 0 (.ptr st.next))
    | .freeG => freeCell st 0
    | .getG x => .ok (st.set (fp + x) (st.cont 0))
    | .newv t => .ok ((st.grow .hview).set (fp + t) (.ptr st.next))
    | .mkalias q =>
      match D[q]? with
      | some (.alias t) => .ok (st.set (fp + q) (st.cont (fp + t)))
      | _ => .trap
    | .load x q => st.reach (st.cont (fp + q)) fun c => .ok (st.set (fp + x) (st.cont c))
    | .store q x => st.reach (st.cont (fp + q)) fun c => .ok (st.set c (st.cont (fp + x)))
    | .gput t => .ok ((st.set 1 (st.cont (fp + t))).set (fp + t) .null)
    | .gget t => .ok ((st.set (fp + t) (st.cont 1)).set 1 .null)
    | .call f args ret =>
      match P[f]? with
      | none => .trap
      | some F =>
        let vals := List.zipWith (fun p v => argVal D fp st p.1 v) F.params args
        -- Pointer-to-cell parameters are nonnull.
        if (List.zipWith (fun p v => p.1 != Ty.bor && v == Val.null) F.params vals).any id then .trap
        else
          (exec P n F.tys st.next (enter st F.tys vals) F.body).then fun st1 =>
            let st2 := leave st1 st.next F.tys.length
            match ret, F.ret with
            | some y, some r => .ok (st2.set (fp + y) (st1.cont (st.next + r)))
            | _, _ => .ok st2

/-! ## Rule switches -/

structure Rules where
  /-- 1. At a call, what `out` points to may afterwards hold any borrow the
  other arguments carry. -/
  flowOut : Bool
  /-- 2. A `_Noescape` parameter may not be stored through an out parameter
  or returned, directly, through a local or through a call. -/
  noEscapeChecked : Bool
  /-- 3. A struct pointer argument carries a borrow of what the struct owns. -/
  interior : Bool
  /-- 4. A borrow of what a global owns may not be stored through an out
  parameter, -/
  noGlobalOut : Bool
  /-- or returned. -/
  noGlobalReturn : Bool
  /-- 5. Nor may a borrow of the body's own local. -/
  noLocalOut : Bool
  /-- 6. A borrow written into views a local owns is added to that local. -/
  viewFlow : Bool
  /-- 7. Not written through any other pointer to those views. -/
  viewOwnerOnly : Bool
  /-- 8. Pointers to views are not kept in globals. -/
  viewNoField : Bool
  /-- 9. The arguments of a call must still be usable once its exclusive
  arguments have been lent, and nothing is lent exclusively twice. -/
  argsApart : Bool
  /-- 10. A call ends every borrow of what a global owns. -/
  callEndsGlobal : Bool
  /-- 11. Two out parameters may point to the same cell. -/
  outsMayAlias : Bool
  /-- 12. What an out parameter points to must still be usable at return. -/
  exitLive : Bool
  /-- 13. A read through a pointer to views sees what the owner's views
  borrow now, not what they did when the pointer was made. -/
  aliasCurrent : Bool

def Rules.full : Rules :=
  ⟨true, true, true, true, true, true, true, true, true, true, true, true, true, true⟩

/-! ## Checker

Per variable, the loans what it holds may come from (for `vown` and `out`,
what the cell it points to holds); `none` once one of them ended. -/

inductive Loan where
  /-- What `v` owns (`v.p`, `v->p`). -/
  | own (v : Nat)
  /-- The local int `v`. -/
  | addr (v : Nat)
  /-- What parameter `v` held at entry. -/
  | par (v : Nat)
  | glob
  deriving DecidableEq, Repr

abbrev Abs := Nat → Option (List Loan)

def kill (l : Loan) (A : Abs) : Abs := fun v =>
  match A v with
  | some L => if l ∈ L then none else some L
  | none => none

def join (A B : Abs) : Abs := fun v =>
  match A v, B v with
  | some L, some M => some (L ++ M)
  | _, _ => none

/-- `p = (pt, noesc)` accepts argument `v`. -/
def argOK (D : List Ty) (pt : Ty) (v : Nat) : Bool :=
  match pt, D[v]? with
  | .bor, some .bor => true
  | .out, some .bor | .out, some .vown | .out, some .out => true
  | .sh, some .own | .sh, some .sh | .sh, some .ex => true
  | .ex, some .own | .ex, some .ex => true
  | _, _ => false

abbrev Args := List ((Ty × Bool) × Nat)

def exArgs (as : Args) : List Nat := as.filterMap fun p => if p.1.1 = .ex then some p.2 else none
def outArgs (as : Args) : List Nat := as.filterMap fun p => if p.1.1 = .out then some p.2 else none
def objArgs (as : Args) : List Nat :=
  as.filterMap fun p => if p.1.1 = .sh ∨ p.1.1 = .ex then some p.2 else none

def killAll (vs : List Nat) (A : Abs) : Abs := vs.foldr (fun v A => kill (.own v) A) A

def argLive (B : Abs) (p : (Ty × Bool) × Nat) : Bool :=
  if p.1.1 = .bor ∨ p.1.1 = .out then (B p.2).isSome else true

/-- What an argument lets the callee keep. -/
def argSrc (R : Rules) (B : Abs) (p : (Ty × Bool) × Nat) : List Loan :=
  if p.1.1 = .out then (B p.2).getD []
  else if p.1.2 then []
  else if p.1.1 = .bor then (B p.2).getD []
  else if R.interior then [.own p.2] else []

def chkCall (R : Rules) (D : List Ty) (A : Abs) (F : Fn) (args : List Nat) (ret : Option Nat) :
    Option Abs :=
  let as := F.params.zip args
  let A0 := if R.callEndsGlobal then kill .glob A else A
  let A1 := killAll (exArgs as) A0
  let B := if R.argsApart then A1 else A
  let src := as.flatMap (argSrc R B)
  let outs := outArgs as
  let anyOut := outs.any fun v => D[v]? = some .out
  let A2 : Abs := fun v =>
    if R.flowOut && (decide (v ∈ outs) || (R.outsMayAlias && anyOut && decide (D[v]? = some .out)))
    then (A1 v).map (src ++ ·) else A1 v
  if args.length = F.params.length ∧ as.all (fun p => argOK D p.1.1 p.2) ∧
      as.all (argLive B) ∧
      (R.argsApart = true → (exArgs as).all (fun v => (objArgs as).count v = 1)) then
    match ret with
    | none => some A2
    | some y => if D[y]? = some .bor ∧ F.ret.isSome then some (upd A2 y (some src)) else none
  else none

def chk (R : Rules) (P : List Fn) (D : List Ty) : Abs → Stmt → Option Abs
  | A, .skip => some A
  | A, .seq a b => (chk R P D A a).bind fun A1 => chk R P D A1 b
  | A, .branch _ a b =>
    (chk R P D A a).bind fun A1 => (chk R P D A b).bind fun A2 => some (join A1 A2)
  | A, .null x => if D[x]? = some .bor then some (upd A x (some [])) else none
  | A, .addr x y =>
    if D[x]? = some .bor ∧ D[y]? = some .int then some (upd A x (some [.addr y])) else none
  | A, .copy x y =>
    if D[x]? = some .bor ∧ D[y]? = some .bor ∧ (A y).isSome then some (upd A x (A y)) else none
  | A, .deref x => if D[x]? = some .bor ∧ (A x).isSome then some A else none
  | A, .new v =>
    if D[v]? = some .own ∨ D[v]? = some .ex then some (kill (.own v) A) else none
  | A, .free v =>
    if D[v]? = some .own ∨ D[v]? = some .ex ∨ D[v]? = some .vown then some (kill (.own v) A)
    else none
  | A, .get x v =>
    if D[x]? = some .bor ∧ (D[v]? = some .own ∨ D[v]? = some .sh ∨ D[v]? = some .ex) then
      some (upd A x (some [.own v])) else none
  | A, .newG => some (kill .glob A)
  | A, .freeG => some (kill .glob A)
  | A, .getG x => if D[x]? = some .bor then some (upd A x (some [.glob])) else none
  | A, .newv t =>
    if D[t]? = some .vown then some (upd (kill (.own t) A) t (some [])) else none
  | A, .mkalias q =>
    match D[q]? with
    | some (.alias t) => if D[t]? = some .vown then
        (A t).bind fun L => some (upd A q (some (.own t :: L))) else none
    | _ => none
  | A, .load x q =>
    if D[x]? = some .bor then
      match D[q]? with
      | some .out | some .vown => (A q).bind fun L => some (upd A x (some L))
      | some (.alias t) => if D[t]? = some .vown then
          (A q).bind fun L =>
            if R.aliasCurrent then (A t).bind fun M => some (upd A x (some M))
            else some (upd A x (some L))
        else none
      | _ => none
    else none
  | A, .store q x =>
    if D[x]? = some .bor then
      (A x).bind fun L =>
        match D[q]? with
        | some .vown => some (if R.viewFlow then upd A q ((A q).map (L ++ ·)) else A)
        | some .out =>
          some fun v => if v = q then some L
            else if R.outsMayAlias ∧ D[v]? = some .out then (A v).map (L ++ ·) else A v
        | some (.alias _) => if R.viewOwnerOnly then none else some A
        | _ => none
    else none
  | A, .gput t =>
    if R.viewNoField = false ∧ D[t]? = some .vown then some (kill (.own t) A) else none
  | A, .gget t =>
    if R.viewNoField = false ∧ D[t]? = some .vown then
      some (upd (kill (.own t) A) t (some [])) else none
  | A, .call f args ret => (P[f]?).bind fun F => chkCall R D A F args ret

/-! ## Function bodies -/

def Fn.noesc (F : Fn) (v : Nat) : Bool := ((F.params[v]?).map (·.2)).getD false

/-- A loan that what an out parameter points to, or the result, may carry. -/
def exitOK (R : Rules) (F : Fn) (isRet : Bool) : Loan → Bool
  | .par v => !R.noEscapeChecked || F.tys[v]? == some .out || !F.noesc v
  | .own v =>
    if F.tys[v]? = some .sh ∨ F.tys[v]? = some .ex then !R.noEscapeChecked || !F.noesc v
    else !R.noLocalOut
  | .addr _ => !R.noLocalOut
  | .glob => if isRet then !R.noGlobalReturn else !R.noGlobalOut

def Abs.entry (D : List Ty) : Abs := fun v =>
  match D[v]? with
  | some .bor | some .out => some [.par v]
  -- Nothing to read through yet.
  | some (.alias _) => none
  | _ => some []

def paramTy : Ty → Bool
  | .bor | .out | .sh | .ex => true
  | _ => false

def localTy : Ty → Bool
  | .int | .bor | .own | .vown | .alias _ => true
  | _ => false

def chkFn (R : Rules) (P : List Fn) (F : Fn) : Bool :=
  F.params.all (fun p => paramTy p.1) && F.locals.all localTy &&
  match chk R P F.tys (Abs.entry F.tys) F.body with
  | none => false
  | some A =>
    (List.range F.tys.length).all (fun v =>
      F.tys[v]? != some .out ||
        match A v with
        | some L => L.all (exitOK R F false)
        | none => !R.exitLive) &&
    match F.ret with
    | none => true
    | some r => F.tys[r]? == some .bor &&
        match A r with
        | some L => L.all (exitOK R F true)
        | none => false

/-- Every body is checked; function 0 is `main`, without parameters. -/
def check (R : Rules) (P : List Fn) : Bool :=
  P.all (chkFn R P) && (match P[0]? with | some F => F.params.isEmpty | none => false)

def run (P : List Fn) (fuel : Nat) : Out := exec P fuel [] 2 St.init (.call 0 [] none)

end SafeC.Out
