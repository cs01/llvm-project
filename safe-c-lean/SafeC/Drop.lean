-- _Drop scope-exit cleanup for Safe C: syntax, semantics with use after free,
-- double free and invalid free as UB, the rule switches and the checker.
namespace SafeC.Drop

/-! ## State

A pointer is null, a heap block (numbered by allocation order), or the
address of a local, which may be dereferenced but never freed. -/

inductive Val where
  | null
  | heap (b : Nat)
  | loc (a : Nat)
  deriving DecidableEq, Repr

structure St where
  /-- Pointer locals. -/
  ρ : Nat → Val
  /-- Heap blocks not yet freed. -/
  alive : Nat → Bool
  /-- The block the next `malloc` returns. -/
  next : Nat

def upd {α : Type} (f : Nat → α) (x : Nat) (v : α) : Nat → α :=
  fun y => if y = x then v else f y

def St.setV (st : St) (x : Nat) (v : Val) : St := { st with ρ := upd st.ρ x v }

/-- Every pointer local starts null; nothing is allocated. -/
def St.init : St := ⟨fun _ => .null, fun _ => false, 0⟩

inductive UB where
  | useAfterFree
  | doubleFree
  /-- `free` of a pointer `malloc` did not return (the address of a local). -/
  | invalidFree
  deriving DecidableEq, Repr

/-- `brk` and `ret` are the early exits: `break` out of the innermost block
and `return` out of all of them. Each block's cleanup runs as they pass. -/
inductive Out where
  | ok (st : St)
  | brk (st : St)
  | ret (st : St)
  | trap
  | ub (k : UB)
  | outOfFuel

def Out.then (o : Out) (f : St → Out) : Out :=
  match o with
  | .ok st => f st
  | o => o

def Out.isUB : Out → Bool
  | .ub _ => true
  | _ => false

/-! ## Syntax -/

inductive Stmt where
  | skip
  | seq (a b : Stmt)
  /-- `if (c) a else b`, for a condition the checker does not look at. -/
  | branch (c : Bool) (a b : Stmt)
  /-- `do { int *x _Drop_free = malloc(n); body } while (0);`: the cleanup
  `free(x)` runs on every exit from the body, and `break` leaves it. -/
  | block (x : Nat) (body : Stmt)
  /-- `y = x` into a plain pointer `y`: a borrow of `x`'s memory. -/
  | copy (y x : Nat)
  /-- `y = &a` for a local `int a`. -/
  | addr (y a : Nat)
  /-- `y = _Take(x)`: moves the pointer into owner `y` and nulls `x`. -/
  | take (y x : Nat)
  /-- `y = x` into an owner `y` (an `_Owned` local, or an owning parameter
  as in `keep(x)`, or a return): a move by name, `x` keeps its value. -/
  | move (y x : Nat)
  | free (x : Nat)
  | deref (x : Nat)
  | brk
  | ret
  deriving Repr

/-! ## Semantics -/

def freeV (st : St) : Val → Out
  | .null => .ok st
  | .loc _ => .ub .invalidFree
  | .heap b => if st.alive b then .ok { st with alive := upd st.alive b false } else .ub .doubleFree

def St.grow (st : St) : St :=
  { st with alive := upd st.alive st.next true, next := st.next + 1 }

/-- `x = malloc(n)`; `malloc` does not fail here. -/
def alloc (st : St) (x : Nat) : St := st.grow.setV x (.heap st.next)

/-- Leave block `x`: its cleanup frees `x` on a fallthrough, a `break` or a
`return`, and the `return` goes on outward. -/
def leave (x : Nat) : Out → Out
  | .ok st => freeV st (st.ρ x)
  | .brk st => freeV st (st.ρ x)
  | .ret st => (freeV st (st.ρ x)).then .ret
  | o => o

def exec : Nat → St → Stmt → Out
  | 0, _, _ => .outOfFuel
  | n + 1, st, s =>
    match s with
    | .skip => .ok st
    | .seq a b => (exec n st a).then fun st' => exec n st' b
    | .branch c a b => if c then exec n st a else exec n st b
    | .block x body => leave x (exec n (alloc st x) body)
    | .copy y x => .ok (st.setV y (st.ρ x))
    | .addr y a => .ok (st.setV y (.loc a))
    | .take y x => .ok ((st.setV x .null).setV y (st.ρ x))
    | .move y x => .ok (st.setV y (st.ρ x))
    | .free x => freeV st (st.ρ x)
    | .deref x =>
      match st.ρ x with
      | .null => .trap
      | .loc _ => .ok st
      | .heap b => if st.alive b then .ok st else .ub .useAfterFree
    | .brk => .brk st
    | .ret => .ret st

/-! ## Rule switches -/

structure Rules where
  /-- A `_Drop` variable is not given away by name, only through `_Take`. -/
  noMoveByName : Bool
  /-- A borrow of a `_Drop` variable dies when the variable's scope ends. -/
  copiesDie : Bool
  /-- A `_Drop` variable is not freed by name inside its scope. -/
  noManualFree : Bool
  /-- A `_Drop` variable is assigned only an owner (`malloc`, `_Take`, a
  move), never a borrow or the address of a local. -/
  dropHoldsOwner : Bool

def Rules.full : Rules := ⟨true, true, true, true⟩

/-! ## Checker

Per pointer local, what it holds on every path here. -/

inductive Av where
  | null
  /-- Null, or a live heap block no other owner holds. -/
  | own
  /-- The same value as owner `x`; valid while `x` owns it. -/
  | borrow (x : Nat)
  | loc
  /-- Nothing known: freed, moved from, or out of scope. -/
  | dead
  deriving DecidableEq, Repr

abbrev Abs := Nat → Av

/-- `x` may be freed, taken or moved: it owns what it holds. -/
def Av.owns : Av → Bool
  | .null | .own => true
  | _ => false

def Av.isDead : Av → Bool
  | .dead => true
  | _ => false

/-- What a plain copy of `x` holds. -/
def Av.val (x : Nat) : Av → Option Av
  | .null => some .null
  | .own => some (.borrow x)
  | .borrow z => some (.borrow z)
  | .loc => some .loc
  | .dead => none

/-- Borrows of `y` die when `y` is written, freed, moved or leaves scope. -/
def killB (y : Nat) (A : Abs) : Abs := fun v =>
  match A v with
  | .borrow z => if z = y then .dead else .borrow z
  | a => a

def Av.join : Av → Av → Av
  | .null, .null => .null
  | .own, .own => .own
  | .own, .null => .own
  | .null, .own => .own
  | .borrow x, .borrow y => if x = y then .borrow x else .dead
  | .loc, .loc => .loc
  | _, _ => .dead

/-- After a join a borrow is kept only while its owner is still `own`. -/
def norm (P : Abs) : Abs := fun v =>
  match P v with
  | .borrow z =>
    match P z with
    | .own => .borrow z
    | _ => .dead
  | a => a

def join (A B : Abs) : Abs := norm fun v => (A v).join (B v)

def ojoin : Option Abs → Option Abs → Option Abs
  | none, none => none
  | some a, none => some a
  | none, some b => some b
  | some a, some b => some (join a b)

/-- The states at the end of a statement: falling through, at a `break`, at
a `return`; `none` when no path gets there. -/
structure Res where
  n : Option Abs
  b : Option Abs
  r : Option Abs

def Res.join (r1 r2 : Res) : Res := ⟨ojoin r1.n r2.n, ojoin r1.b r2.b, ojoin r1.r r2.r⟩

def only (A : Abs) : Res := ⟨some A, none, none⟩

/-- `D` is the list of `_Drop` variables in scope. A block may not redeclare
one of them (C would shadow it; the model asks for distinct names). -/
def chk (R : Rules) : List Nat → Abs → Stmt → Option Res
  | _, A, .skip => some (only A)
  | D, A, .seq a b => (chk R D A a).bind fun r1 =>
    match r1.n with
    | none => some r1
    | some A1 => (chk R D A1 b).bind fun r2 => some ⟨r2.n, ojoin r1.b r2.b, ojoin r1.r r2.r⟩
  | D, A, .branch _ a b => (chk R D A a).bind fun r1 => (chk R D A b).bind fun r2 => some (r1.join r2)
  | D, A, .block x body =>
    if D.contains x then none else
    (chk R (x :: D) (upd (killB x A) x .own) body).bind fun r =>
      let ex := fun A' => upd (if R.copiesDie then killB x A' else A') x .dead
      some ⟨(ojoin r.n r.b).map ex, none, r.r.map ex⟩
  | D, A, .copy y x =>
    if y = x || (R.dropHoldsOwner && D.contains y) then none else
    ((killB y A x).val x).map fun a => only (upd (killB y A) y a)
  | D, A, .addr y _ =>
    if R.dropHoldsOwner && D.contains y then none else some (only (upd (killB y A) y .loc))
  | _, A, .take y x =>
    if y = x || !(A x).owns then none else
    let A1 := upd (killB x A) x .null
    some (only (upd (killB y A1) y (A x)))
  | D, A, .move y x =>
    if y = x || !(A x).owns || (R.noMoveByName && D.contains x) then none else
    let A1 := upd (killB x A) x .dead
    some (only (upd (killB y A1) y (A x)))
  | D, A, .free x =>
    if !(A x).owns || (R.noManualFree && D.contains x) then none else
    some (only (upd (killB x A) x .dead))
  | _, A, .deref x => if (A x).isDead then none else some (only A)
  | _, A, .brk => some ⟨none, some A, none⟩
  | _, A, .ret => some ⟨none, none, some A⟩

def Abs.init : Abs := fun _ => .null

def check (R : Rules) (s : Stmt) : Option Res := chk R [] Abs.init s

def run (s : Stmt) : Out := exec 1000 St.init s

end SafeC.Drop
