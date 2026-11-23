-- Syntax, operational semantics and typing rules of the core Safe C model.
namespace SafeC

/-! ## Types -/

inductive Null where
  | nonnull
  | nullable
  deriving DecidableEq, Repr

/-- Pointer kinds. `counted n` has its count fixed at `n`: Safe C reads the
count from a variable, which is the same thing once that variable may not
change while the pointer is live (see README). -/
inductive Kind where
  | single
  | counted (n : Nat)
  | indexable
  | nullterm
  deriving DecidableEq, Repr

inductive Ty where
  | int
  | char
  | void
  | ptr (q : Null) (k : Kind) (t : Ty)
  deriving DecidableEq, Repr

/-- Types whose values hold no pointer; any bit pattern is a valid value. -/
def Ty.ptrFree : Ty → Bool
  | .int => true
  | .char => true
  | _ => false

def Ty.isNonnullPtr : Ty → Bool
  | .ptr .nonnull _ _ => true
  | _ => false

/-- An unannotated pointer in an interface is nonnull and single. -/
def Ty.iface (t : Ty) : Ty := .ptr .nonnull .single t

/-! ## Values and memory -/

/-- Pointers are (block, offset) plus a lower/upper bound. Only indexable
pointers (and bounded null-terminated ones, see `Rules.ntBounded`) consult the
bounds at run time; for the other kinds they are dead fields. -/
inductive Val where
  | int (n : Int)
  | ptr (b : Nat) (off lo hi : Int)
  | null
  | uninit
  deriving DecidableEq, Repr

structure Block where
  /-- Element type at allocation. Ghost: the semantics never reads it, only
  the soundness invariant does. -/
  ty : Ty
  cells : List Val

abbrev Mem := List Block
abbrev Var := Nat
abbrev Env := Var → Val

def Env.set (ρ : Env) (x : Var) (v : Val) : Env := fun y => if y = x then v else ρ y

inductive Res (α : Type) where
  | ok (a : α)
  | trap
  | ub

def Res.bind {α β : Type} : Res α → (α → Res β) → Res β
  | .ok a, f => f a
  | .trap, _ => .trap
  | .ub, _ => .ub

/-! ## Syntax -/

inductive Op where
  | add | sub | mul | div | lt
  deriving DecidableEq, Repr

inductive Expr where
  | lit (n : Int)
  | null (k : Kind) (t : Ty)
  | var (x : Var)
  | bin (op : Op) (a b : Expr)
  /-- `p[i]` through a pointer of kind `k`; `*p` is `index k p (lit 0)`. -/
  | index (k : Kind) (p i : Expr)
  /-- `p + i`. -/
  | padd (k : Kind) (p i : Expr)
  /-- Convert a kind-`k` pointer to kind `k'` (bounds checks, terminator scan). -/
  | conv (k k' : Kind) (p : Expr)
  /-- Change the pointee type: `(t *) p`. Also stands for reading a union
  member of another type, which reinterprets the same storage. -/
  | cast (t : Ty) (p : Expr)
  deriving Repr

inductive Stmt where
  | skip
  | seq (a b : Stmt)
  | assign (x : Var) (e : Expr)
  /-- `p[i] = e`. -/
  | store (k : Kind) (p i e : Expr)
  /-- `x = (t *) calloc(n, sizeof(t))`: fresh, zero-filled memory. -/
  | alloc (x : Var) (t : Ty) (n : Nat)
  | ifz (c : Expr) (a b : Stmt)
  /-- `if (x == NULL) a else b`. -/
  | ifNull (x : Var) (a b : Stmt)
  | loop (c : Expr) (body : Stmt)
  | unsafeBlk (s : Stmt)
  /-- Call a function with body `body`; it sees the same variables (all of
  them act as globals). -/
  | call (body : Stmt)
  deriving Repr

def Stmt.noUnsafe : Stmt → Bool
  | .seq a b => a.noUnsafe && b.noUnsafe
  | .ifz _ a b => a.noUnsafe && b.noUnsafe
  | .ifNull _ a b => a.noUnsafe && b.noUnsafe
  | .loop _ b => b.noUnsafe
  | .call b => b.noUnsafe
  | .unsafeBlk _ => false
  | _ => true

/-! ## Rule switches

Each field is one design decision the review questioned. `Rules.current` is
the rule set as of the review; `Rules.Sound` (in Sound.lean) is the condition
under which the soundness proof goes through. -/

structure Rules where
  /-- Every null access traps (`-fsanitize=null`, trap mode). If false, a null
  access is UB and soundness relies on the nonnull type being true. -/
  nullTrap : Bool
  /-- Zero-filled storage may have a nonnull pointer element type. -/
  zeroNonnull : Bool
  /-- Casts between any two non-void pointee types are accepted. -/
  castAny : Bool
  /-- Null narrowing facts survive a call. -/
  callKeepsNarrowing : Bool
  /-- Null-terminated pointers keep the bound found by the checked conversion
  and every access through them is checked against it. If false they are thin:
  only the terminator is trusted. -/
  ntBounded : Bool
  deriving Repr

def Rules.current : Rules :=
  { nullTrap := true, zeroNonnull := true, castAny := true,
    callKeepsNarrowing := true, ntBounded := false }

/-- The rules before the review fixed hole 1. -/
def Rules.preReview : Rules := { Rules.current with nullTrap := false }

/-! ## Semantics

`raw = true` inside `__unsafe`: plain C, where the checks are absent and the
violations they would catch are UB. -/

def INT_MIN : Int := -2147483648
def INT_MAX : Int := 2147483647
def wrap (n : Int) : Int := (n + 2147483648) % 4294967296 - 2147483648

def arith (raw : Bool) (op : Op) (x y : Int) : Res Val :=
  match op with
  | .add => if raw = true ∧ ¬(INT_MIN ≤ x + y ∧ x + y ≤ INT_MAX) then .ub else .ok (.int (wrap (x + y)))
  | .sub => if raw = true ∧ ¬(INT_MIN ≤ x - y ∧ x - y ≤ INT_MAX) then .ub else .ok (.int (wrap (x - y)))
  | .mul => if raw = true ∧ ¬(INT_MIN ≤ x * y ∧ x * y ≤ INT_MAX) then .ub else .ok (.int (wrap (x * y)))
  | .div => if y = 0 ∨ (x = INT_MIN ∧ y = -1) then (if raw = true then .ub else .trap)
            else .ok (.int (wrap (x.tdiv y)))
  | .lt => .ok (.int (if x < y then 1 else 0))

def nullAccess (R : Rules) (raw : Bool) {α : Type} : Res α :=
  if R.nullTrap = true ∧ raw = false then .trap else .ub

/-- Address of element `i` of pointer `v` of kind `k`, with the checks the
compiler inserts for that kind. -/
def addr (R : Rules) (raw : Bool) (k : Kind) (v : Val) (i : Int) : Res (Nat × Int) :=
  match v with
  | .null => nullAccess R raw
  | .ptr b off lo hi =>
    if raw = true then .ok (b, off + i) else
    match k with
    | .single => if i = 0 then .ok (b, off) else .trap
    | .counted n => if 0 ≤ i ∧ i < n then .ok (b, off + i) else .trap
    | .indexable => if lo ≤ off + i ∧ off + i < hi then .ok (b, off + i) else .trap
    | .nullterm =>
      if R.ntBounded = true then
        (if lo ≤ off + i ∧ off + i < hi then .ok (b, off + i) else .trap)
      else (if i = 0 then .ok (b, off) else .trap)
  | _ => .ub

/-- Raw memory read: outside the block is UB. -/
def rd (μ : Mem) (b : Nat) (o : Int) : Res Val :=
  match μ[b]? with
  | some blk => if 0 ≤ o then (match blk.cells[o.toNat]? with
      | some v => .ok v
      | none => .ub) else .ub
  | none => .ub

/-- Raw memory write: outside the block is UB. -/
def wr (μ : Mem) (b : Nat) (o : Int) (v : Val) : Res Mem :=
  match μ[b]? with
  | some blk =>
    if 0 ≤ o ∧ o < blk.cells.length then
      .ok (μ.set b { blk with cells := blk.cells.set o.toNat v })
    else .ub
  | none => .ub

/-- `p + i`. Indexable pointers may go out of bounds (accesses are checked);
null-terminated pointers only step by one and never past the terminator. -/
def step (R : Rules) (μ : Mem) (k : Kind) : Val → Val → Res Val
  | .null, _ => nullAccess R false
  | .ptr b off lo hi, .int n =>
    match k with
    | .indexable => .ok (.ptr b (off + n) lo hi)
    | .nullterm =>
      if n ≠ 1 then .trap
      else if R.ntBounded = true ∧ ¬(lo ≤ off ∧ off < hi) then .trap
      else (rd μ b off).bind fun c =>
        if c = .int 0 then .trap else .ok (.ptr b (off + 1) lo hi)
    | _ => .trap
  | _, _ => .ub

/-- First index `j ≥ start`, `j < start + k`, whose cell is the terminator. -/
def findZero (cells : List Val) : Nat → Nat → Option Nat
  | _, 0 => none
  | j, k + 1 => if cells[j]? = some (.int 0) then some j else findZero cells (j + 1) k

/-- Bounds a pointer of kind `k` is known to have, as an indexable range. A thin
null-terminated pointer has none. -/
def toIdx (R : Rules) (k : Kind) (off lo hi : Int) : Option (Int × Int) :=
  match k with
  | .single => some (off, off + 1)
  | .counted n => some (off, off + n)
  | .indexable => some (lo, hi)
  | .nullterm => if R.ntBounded = true then some (lo, hi) else none

/-- Kind conversion. To null-terminated: scan `[off, hi)` for a terminator,
trap if there is none, and keep `off .. j+1` as the bound. -/
def convV (R : Rules) (μ : Mem) (k k' : Kind) : Val → Res Val
  | .null => .ok .null
  | .ptr b off lo hi =>
    if k = k' then .ok (.ptr b off lo hi) else
    match toIdx R k off lo hi with
    | none => .trap
    | some (l, h) =>
      match k' with
      | .indexable => .ok (.ptr b off l h)
      | .single => if l ≤ off ∧ off < h then .ok (.ptr b off l h) else .trap
      | .counted n => if l ≤ off ∧ off + n ≤ h then .ok (.ptr b off l h) else .trap
      | .nullterm =>
        if l ≤ off ∧ off ≤ h then
          match μ[b]? with
          | none => .ub
          | some blk =>
            if 0 ≤ l ∧ h ≤ blk.cells.length then
              match findZero blk.cells off.toNat (h - off).toNat with
              | some j => .ok (.ptr b off off (j + 1))
              | none => .trap
            else .ub
        else .trap
  | _ => .ub

def evalE (R : Rules) (raw : Bool) (ρ : Env) (μ : Mem) : Expr → Res Val
  | .lit n => .ok (.int n)
  | .null _ _ => .ok .null
  | .var x => if ρ x = .uninit then .ub else .ok (ρ x)
  | .bin op a b =>
    (evalE R raw ρ μ a).bind fun va => (evalE R raw ρ μ b).bind fun vb =>
      match va, vb with
      | .int x, .int y => arith raw op x y
      | _, _ => .ub
  | .index k p i =>
    (evalE R raw ρ μ p).bind fun vp => (evalE R raw ρ μ i).bind fun vi =>
      match vi with
      | .int n => (addr R raw k vp n).bind fun a => rd μ a.1 a.2
      | _ => .ub
  | .padd k p i =>
    (evalE R raw ρ μ p).bind fun vp => (evalE R raw ρ μ i).bind fun vi => step R μ k vp vi
  | .conv k k' p => (evalE R raw ρ μ p).bind fun vp => convV R μ k k' vp
  | .cast _ p => evalE R raw ρ μ p

def storeR (R : Rules) (raw : Bool) (ρ : Env) (μ : Mem) (k : Kind) (p i e : Expr) : Res Mem :=
  (evalE R raw ρ μ p).bind fun vp => (evalE R raw ρ μ i).bind fun vi =>
    (evalE R raw ρ μ e).bind fun ve =>
      match vi with
      | .int n => (addr R raw k vp n).bind fun a => wr μ a.1 a.2 ve
      | _ => .ub

def zeroOf : Ty → Val
  | .ptr _ _ _ => .null
  | _ => .int 0

inductive Out where
  | ok (ρ : Env) (μ : Mem)
  | trap
  | ub
  | outOfFuel

def Out.isUB : Out → Bool
  | .ub => true
  | _ => false

def exec (R : Rules) : Bool → Nat → Env → Mem → Stmt → Out
  | _, 0, _, _, _ => .outOfFuel
  | raw, n + 1, ρ, μ, s =>
    match s with
    | .skip => .ok ρ μ
    | .seq a b =>
      match exec R raw n ρ μ a with
      | .ok ρ' μ' => exec R raw n ρ' μ' b
      | o => o
    | .assign x e =>
      match evalE R raw ρ μ e with
      | .ok v => .ok (ρ.set x v) μ
      | .trap => .trap
      | .ub => .ub
    | .store k p i e =>
      match storeR R raw ρ μ k p i e with
      | .ok μ' => .ok ρ μ'
      | .trap => .trap
      | .ub => .ub
    | .alloc x t m =>
      .ok (ρ.set x (.ptr μ.length 0 0 m)) (μ ++ [⟨t, List.replicate m (zeroOf t)⟩])
    | .ifz c a b =>
      match evalE R raw ρ μ c with
      | .ok (.int z) => if z ≠ 0 then exec R raw n ρ μ a else exec R raw n ρ μ b
      | .ok _ => .ub
      | .trap => .trap
      | .ub => .ub
    | .ifNull x a b =>
      match ρ x with
      | .null => exec R raw n ρ μ a
      | .ptr _ _ _ _ => exec R raw n ρ μ b
      | _ => .ub
    | .loop c body =>
      match evalE R raw ρ μ c with
      | .ok (.int z) =>
        if z = 0 then .ok ρ μ else
        match exec R raw n ρ μ body with
        | .ok ρ' μ' => exec R raw n ρ' μ' (.loop c body)
        | o => o
      | .ok _ => .ub
      | .trap => .trap
      | .ub => .ub
    | .unsafeBlk s => exec R true n ρ μ s
    | .call body => exec R raw n ρ μ body

/-! ## Typing

Flow-sensitive: `Facts` records which variables are definitely initialized
and which nullable pointers are narrowed to nonnull by a null check. -/

structure Facts where
  init : List Var
  nn : List Var

abbrev Decls := List (Var × Ty)

def Facts.assign (F : Facts) (x : Var) : Facts := ⟨x :: F.init, F.nn.filter (· ≠ x)⟩
def Facts.meet (F G : Facts) : Facts := ⟨F.init.filter (· ∈ G.init), F.nn.filter (· ∈ G.nn)⟩
def Facts.empty : Facts := ⟨[], []⟩

def narrow (F : Facts) (x : Var) : Ty → Ty
  | .ptr q k t => if x ∈ F.nn then .ptr .nonnull k t else .ptr q k t
  | τ => τ

/-- Pointee casts. To `void` is always fine. From `void` only via `alloc`
(fresh allocation). Otherwise `castAny`, or the sound rule: same pointee, or
both pointer-free. -/
def castOK (R : Rules) (u : Bool) (t t' : Ty) : Bool :=
  u || t' == .void || (t != .void && (R.castAny || t == t' || (t.ptrFree && t'.ptrFree)))

/-- Assignment compatibility: equal, both pointer-free, or nonnull to nullable
at the top level only (pointees are invariant). -/
def Sub : Ty → Ty → Prop
  | .ptr .nonnull k t, .ptr .nullable k' t' => k = k' ∧ t = t'
  | τ, σ => τ = σ ∨ (τ.ptrFree = true ∧ σ.ptrFree = true)

inductive HasTy (R : Rules) (u : Bool) (D : Decls) (F : Facts) : Expr → Ty → Prop
  | lit : HasTy R u D F (.lit n) .int
  | null : HasTy R u D F (.null k t) (.ptr .nullable k t)
  | var : D.lookup x = some σ → x ∈ F.init → τ = narrow F x σ → HasTy R u D F (.var x) τ
  | bin : HasTy R u D F a τa → HasTy R u D F b τb → τa.ptrFree = true → τb.ptrFree = true →
      HasTy R u D F (.bin op a b) .int
  | index : HasTy R u D F p (.ptr .nonnull k t) → HasTy R u D F i τi → τi.ptrFree = true →
      t ≠ .void → HasTy R u D F (.index k p i) t
  | padd : HasTy R u D F p (.ptr .nonnull k t) → HasTy R u D F i τi → τi.ptrFree = true →
      HasTy R u D F (.padd k p i) (.ptr .nonnull k t)
  | conv : HasTy R u D F p (.ptr q k t) → HasTy R u D F (.conv k k' p) (.ptr q k' t)
  | cast : HasTy R u D F p (.ptr q k t) → castOK R u t t' = true →
      HasTy R u D F (.cast t' p) (.ptr q k t')

inductive HasTyS (R : Rules) (D : Decls) : Bool → Facts → Stmt → Facts → Prop
  | skip : HasTyS R D u F .skip F
  | seq : HasTyS R D u F a F1 → HasTyS R D u F1 b F2 → HasTyS R D u F (.seq a b) F2
  | assign : D.lookup x = some σ → HasTy R u D F e τ → Sub τ σ →
      HasTyS R D u F (.assign x e) (F.assign x)
  | store : HasTy R u D F p (.ptr .nonnull k t) → HasTy R u D F i τi → τi.ptrFree = true →
      t ≠ .void → HasTy R u D F e τ → Sub τ t → HasTyS R D u F (.store k p i e) F
  | alloc : D.lookup x = some (.ptr q .indexable t) → t ≠ .void →
      (R.zeroNonnull = true ∨ t.isNonnullPtr = false) → HasTyS R D u F (.alloc x t n) (F.assign x)
  | ifz : HasTy R u D F c τ → τ.ptrFree = true → HasTyS R D u F a Fa → HasTyS R D u F b Fb →
      HasTyS R D u F (.ifz c a b) (Fa.meet Fb)
  | ifNull : D.lookup x = some (.ptr q k t) → x ∈ F.init → HasTyS R D u F a Fa →
      HasTyS R D u ⟨F.init, x :: F.nn⟩ b Fb → HasTyS R D u F (.ifNull x a b) (Fa.meet Fb)
  | loop : HasTy R u D F c τ → τ.ptrFree = true → HasTyS R D u F body F' →
      F.init ⊆ F'.init → F.nn ⊆ F'.nn → HasTyS R D u F (.loop c body) F
  | unsafeBlk : HasTyS R D true F s F' → HasTyS R D u F (.unsafeBlk s) F'
  /-- The callee is checked on its own, with the caller's initialized
  variables and no narrowing. -/
  | call : HasTyS R D false ⟨F.init, []⟩ body Fc → F.init ⊆ Fc.init →
      HasTyS R D u F (.call body) (if R.callKeepsNarrowing = true then F else ⟨F.init, []⟩)
  | weaken : HasTyS R D u F s F' → G.init ⊆ F'.init → G.nn ⊆ F'.nn → HasTyS R D u F s G

end SafeC
