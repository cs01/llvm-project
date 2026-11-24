-- _Err_or pointers (the kernel ERR_PTR idiom) for Safe C: syntax, semantics
-- with error and null dereference and PTR_ERR of a non-error as UB, the rule
-- switches and the checker.
namespace SafeC.Err

/-! ## State

A pointer local holds null, a valid pointer to object `c`, or an error
pointer carrying errno `e` (`ERR_PTR(-e)`). Valid pointers are never freed
here; lifetimes are the other models' concern. -/

inductive Val where
  | null
  | ptr (c : Nat)
  | err (e : Nat)
  deriving DecidableEq, Repr

structure St where
  ρ : Nat → Val

def upd {α : Type} (f : Nat → α) (x : Nat) (v : α) : Nat → α :=
  fun y => if y = x then v else f y

def St.setV (st : St) (x : Nat) (v : Val) : St := ⟨upd st.ρ x v⟩

def St.init : St := ⟨fun _ => .null⟩

/-- Null dereference is UB here, not a trap, as Safe C's nullability rules
treat it: an `_Err_or` pointer that may also be null needs both checks.
`PTR_ERR` of a pointer that is not an error is a wrong value rather than a
memory error (`PTR_ERR(NULL)` is 0, "success"), but the rule promises it
never happens, so it is counted with the UB. -/
inductive UB where
  | nullDeref
  | errDeref
  | notErr
  deriving DecidableEq, Repr

/-- `trap` is a callee that broke its own return contract (a non-nullable
`_Err_or` function returned null): not this function's fault. -/
inductive Out where
  | ok (st : St)
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

/-- The conditions the checker reads: `IS_ERR(x)`, `IS_ERR_OR_NULL(x)`,
`x == NULL`. -/
inductive Test where
  | isErr
  | isErrOrNull
  | isNull
  deriving DecidableEq, Repr

def Test.eval : Test → Val → Bool
  | .isErr, .err _ => true
  | .isErrOrNull, .err _ => true
  | .isErrOrNull, .null => true
  | .isNull, .null => true
  | _, _ => false

inductive Stmt where
  | skip
  | seq (a b : Stmt)
  /-- `if (c) a else b`, for a condition the checker does not look at. -/
  | branch (c : Bool) (a b : Stmt)
  /-- `if (t(x)) a else b`. -/
  | test (t : Test) (x : Nat) (a b : Stmt)
  /-- `x = f()` for `T *_Err_or f(void)` (or `T *_Nullable _Err_or` when
  `nullable`); `v` is what this run's callee returns. -/
  | call (x : Nat) (nullable : Bool) (v : Val)
  /-- `x = ERR_PTR(-e)`. -/
  | mkErr (x e : Nat)
  /-- `x = &obj`. -/
  | addr (x c : Nat)
  /-- `y = x`. -/
  | copy (y x : Nat)
  /-- `*x`, `x->f`, `x[i]`. -/
  | deref (x : Nat)
  /-- `g(x)` for `void g(T *p)`: the parameter is not `_Err_or`, and the
  callee dereferences it. -/
  | pass (x : Nat)
  /-- `PTR_ERR(x)`. -/
  | ptrErr (x : Nat)
  | ret
  deriving Repr

/-! ## Semantics -/

def derefV (st : St) : Val → Out
  | .null => .ub .nullDeref
  | .err _ => .ub .errDeref
  | .ptr _ => .ok st

def exec : Nat → St → Stmt → Out
  | 0, _, _ => .outOfFuel
  | n + 1, st, s =>
    match s with
    | .skip => .ok st
    | .seq a b => (exec n st a).then fun st' => exec n st' b
    | .branch c a b => if c then exec n st a else exec n st b
    | .test t x a b => if t.eval (st.ρ x) then exec n st a else exec n st b
    | .call x nl v => if v = .null && !nl then .trap else .ok (st.setV x v)
    | .mkErr x e => .ok (st.setV x (.err e))
    | .addr x c => .ok (st.setV x (.ptr c))
    | .copy y x => .ok (st.setV y (st.ρ x))
    | .deref x => derefV st (st.ρ x)
    | .pass x => derefV st (st.ρ x)
    | .ptrErr x =>
      match st.ρ x with
      | .err _ => .ok st
      | _ => .ub .notErr
    | .ret => .ret st

/-! ## Rule switches -/

structure Rules where
  /-- A dereference needs `!IS_ERR(x)` known, not only `x != NULL`. -/
  derefNeedsCheck : Bool
  /-- At a join, "may be an error" holds if it holds on any incoming path. -/
  joinKeepsMaybe : Bool
  /-- A write gives the destination the source's kind (a copy of an
  `_Err_or` value may be an error; re-assigning a checked variable from an
  `_Err_or` source un-checks it), instead of keeping the destination's old
  error fact. -/
  copyKeepsKind : Bool
  /-- Writing `y` ends the record "`v` holds the same value as `y`" for every
  `v`, so testing `y` no longer narrows a stale copy. -/
  aliasDiesOnWrite : Bool
  /-- Passing to a non-`_Err_or` parameter needs `!IS_ERR(x)` known. -/
  paramNeedsCheck : Bool
  /-- `!IS_ERR(x)` narrows only the error fact, not null. -/
  isErrKeepsNull : Bool
  /-- `PTR_ERR(x)` needs `IS_ERR(x)` known: not null, not a valid pointer. -/
  ptrErrNeedsErr : Bool

def Rules.full : Rules := ⟨true, true, true, true, true, true, true⟩

/-! ## Checker

Per pointer local, which kinds of value it may hold here, and optionally
another local known to hold the same value (the null analysis's alias map). -/

structure K where
  n : Bool
  e : Bool
  p : Bool
  deriving DecidableEq, Repr

structure Abs where
  k : Nat → K
  al : Nat → Option Nat

def K.join (R : Rules) (a b : K) : K :=
  ⟨a.n || b.n, if R.joinKeepsMaybe then a.e || b.e else a.e && b.e, a.p || b.p⟩

/-- What a branch learns about `x` from `t(x)` being `b`. -/
def Test.nar (R : Rules) : Test → Bool → K → K
  | .isErr, true, k => ⟨false, k.e, false⟩
  | .isErr, false, k => ⟨k.n && R.isErrKeepsNull, false, k.p⟩
  | .isErrOrNull, true, k => ⟨k.n, k.e, false⟩
  | .isErrOrNull, false, k => ⟨false, false, k.p⟩
  | .isNull, true, k => ⟨k.n, false, false⟩
  | .isNull, false, k => ⟨false, k.e, k.p⟩

/-- Aliases are flattened when recorded: `y = x` records `x`'s own target. -/
def root (A : Abs) (x : Nat) : Nat := (A.al x).getD x

/-- `v` is known to hold the same value as `x`. -/
def same (A : Abs) (x v : Nat) : Bool :=
  v == x || v == root A x || A.al v == some (root A x)

/-- A test of `x` narrows `x` and everything known to hold its value
(`narrowVarWithAliases`). -/
def narrow (A : Abs) (x : Nat) (f : K → K) : Abs :=
  { A with k := fun v => if same A x v then f (A.k v) else A.k v }

/-- Drop every alias record that names `y` as the target. -/
def kill (A : Abs) (y : Nat) : Abs :=
  { A with al := fun v =>
      match A.al v with
      | some t => if t = y then none else some t
      | none => none }

/-- `y` gets a value of kind `k`, known equal to `src` if given. -/
def write (R : Rules) (A : Abs) (y : Nat) (k : K) (src : Option Nat) : Abs :=
  let A1 := if R.aliasDiesOnWrite then kill A y else A
  let k' := if R.copyKeepsKind then k else { k with e := (A.k y).e }
  ⟨upd A1.k y k', upd A1.al y src⟩

def Abs.join (R : Rules) (A B : Abs) : Abs :=
  ⟨fun v => (A.k v).join R (B.k v), fun v => if A.al v = B.al v then A.al v else none⟩

/-- The state at fallthrough; `none` when every path returned. -/
abbrev Res := Option Abs

def ojoin (R : Rules) : Res → Res → Res
  | none, none => none
  | some a, none => some a
  | none, some b => some b
  | some a, some b => some (a.join R b)

def chk (R : Rules) : Abs → Stmt → Option Res
  | A, .skip => some (some A)
  | A, .seq a b => (chk R A a).bind fun r =>
    match r with
    | none => some none
    | some A1 => chk R A1 b
  | A, .branch _ a b =>
    (chk R A a).bind fun r1 => (chk R A b).bind fun r2 => some (ojoin R r1 r2)
  | A, .test t x a b =>
    (chk R (narrow A x (t.nar R true)) a).bind fun r1 =>
      (chk R (narrow A x (t.nar R false)) b).bind fun r2 => some (ojoin R r1 r2)
  | A, .call x nl _ => some (some (write R A x ⟨nl, true, true⟩ none))
  | A, .mkErr x _ => some (some (write R A x ⟨false, true, false⟩ none))
  | A, .addr x _ => some (some (write R A x ⟨false, false, true⟩ none))
  | A, .copy y x => if y = x then none else some (some (write R A y (A.k x) (some (root A x))))
  | A, .deref x => if (A.k x).n || (R.derefNeedsCheck && (A.k x).e) then none else some (some A)
  | A, .pass x => if (A.k x).n || (R.paramNeedsCheck && (A.k x).e) then none else some (some A)
  | A, .ptrErr x =>
    if R.ptrErrNeedsErr && ((A.k x).n || (A.k x).p) then none else some (some A)
  | _, .ret => some none

def Abs.init : Abs := ⟨fun _ => ⟨true, false, false⟩, fun _ => none⟩

def check (R : Rules) (s : Stmt) : Option Res := chk R Abs.init s

def run (s : Stmt) : Out := exec 1000 St.init s

end SafeC.Err
