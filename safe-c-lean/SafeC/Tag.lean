-- Tagged unions for Safe C (M4): syntax, semantics with type confusion as UB,
-- the rule switches and the path-sensitive checker.
namespace SafeC.Tag

/-! ## Layout

One struct type, used by every struct variable:
`struct { int tag; union _Tagged_by(tag) { ... }; }`. `ty` gives each member's
type; `of[V]` is the member that tag value `V` makes live. Because `of` is a
function of the tag, a value names at most one member, which is the compiler's
"values are distinct" check. A member with several `_When` values appears
several times in `of`. -/

inductive MTy where
  | int
  | ptr
  | dbl
  deriving DecidableEq, Repr

structure Lay where
  ty : List MTy
  of : List Nat
  deriving Repr

def Lay.mty (L : Lay) (m : Nat) : MTy := L.ty.getD m .int

/-- Number of tag values that name a member. -/
def Lay.n (L : Lay) : Nat := L.of.length

/-- The tag value `t` makes member `m` live. -/
def Lay.live (L : Lay) (t m : Nat) : Bool := L.of[t]? == some m

/-! ## State

A struct's storage: the tag, and the member whose bytes the union holds with
its payload. The member index is the ghost record of who wrote the bytes; the
semantics uses it only to detect a read as another type. -/

structure SV where
  tag : Nat
  mem : Nat
  val : Int
  deriving DecidableEq, Repr

structure St where
  /-- Struct variables. -/
  σ : Nat → SV
  /-- Scalar locals. -/
  ρ : Nat → Int
  /-- Pointer-to-struct locals: the struct they point at, or null. -/
  ptr : Nat → Option Nat
  /-- Structs whose address has escaped (address taken, or a global): the
  only ones a callee can reach. -/
  esc : Nat → Bool

def upd {α : Type} (f : Nat → α) (x : Nat) (v : α) : Nat → α :=
  fun y => if y = x then v else f y

def St.setS (st : St) (x : Nat) (v : SV) : St := { st with σ := upd st.σ x v }

/-- Every struct starts as `{0}`: tag 0, member 0 holding 0. The globals in
`G` have escaped from the start. -/
def St.init (G : List Nat) : St :=
  ⟨fun _ => ⟨0, 0, 0⟩, fun _ => 0, fun _ => none, fun x => G.contains x⟩

inductive UB where
  | typeConfusion
  deriving DecidableEq, Repr

inductive Out where
  | ok (st : St)
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

/-- A whole-struct write `{.tag = V, .m = v}` done by a callee to struct `x`. -/
structure W where
  x : Nat
  V : Nat
  m : Nat
  v : Int
  deriving Repr

inductive Stmt where
  | skip
  | seq (a b : Stmt)
  /-- `x.tag = V`. -/
  | settag (x V : Nat)
  /-- `x.m = v`. -/
  | mset (x m : Nat) (v : Int)
  /-- `y = x.m`. -/
  | mget (y x m : Nat)
  /-- `if (x.tag == V) a else b`. -/
  | ifTag (x V : Nat) (a b : Stmt)
  /-- `x = (struct S){.tag = V, .m = v}`: an initializer or compound literal. -/
  | init (x V m : Nat) (v : Int)
  /-- `q = &x`. -/
  | addr (q x : Nat)
  /-- `*q = (struct S){.tag = V, .m = v}`: a write through a pointer. -/
  | pset (q V m : Nat) (v : Int)
  /-- `y = q->m`: a read through a place whose tag is not followed (an array
  element `a[j].m`, a call result, a pointer). Rejected by
  `Rules.untrackedRejected`. -/
  | pget (y q m : Nat)
  /-- A call to a function the checker does not look into. It performs the
  listed whole-struct writes to those targets that have escaped; the others
  it cannot name. -/
  | call (ws : List W)
  deriving Repr

/-- `switch (x.tag) { case V1: s1; ... default: d }`, without fallthrough, is
the chain of tests it compiles to. -/
def switchS (x : Nat) : List (Nat × Stmt) → Stmt → Stmt
  | [], d => d
  | (V, s) :: cs, d => .ifTag x V s (switchS x cs d)

/-! ## Semantics -/

/-- Read member `m` of storage `c` into scalar `y`. The bytes are another
member's when `c.mem ≠ m`; with a different type that is type confusion
(an integer read as a pointer, then dereferenced). -/
def readM (L : Lay) (st : St) (y : Nat) (c : SV) (m : Nat) : Out :=
  if c.mem ≠ m ∧ L.mty c.mem ≠ L.mty m then .ub .typeConfusion
  else .ok { st with ρ := upd st.ρ y c.val }

def callW (st : St) : List W → St
  | [] => st
  | w :: ws => callW (if st.esc w.x then st.setS w.x ⟨w.V, w.m, w.v⟩ else st) ws

/-- Fuel-bounded big-step execution; `outOfFuel` stands for running out. -/
def exec (L : Lay) : Nat → St → Stmt → Out
  | 0, _, _ => .outOfFuel
  | n + 1, st, s =>
    match s with
    | .skip => .ok st
    | .seq a b => (exec L n st a).then fun st' => exec L n st' b
    | .settag x V => .ok (st.setS x { st.σ x with tag := V })
    | .mset x m v => .ok (st.setS x { st.σ x with mem := m, val := v })
    | .mget y x m => readM L st y (st.σ x) m
    | .ifTag x V a b => if (st.σ x).tag = V then exec L n st a else exec L n st b
    | .init x V m v => .ok (st.setS x ⟨V, m, v⟩)
    | .addr q x => .ok { st with ptr := upd st.ptr q (some x), esc := upd st.esc x true }
    | .pset q V m v =>
      match st.ptr q with
      | none => .trap
      | some x => .ok (st.setS x ⟨V, m, v⟩)
    | .pget y q m =>
      match st.ptr q with
      | none => .trap
      | some x => readM L st y (st.σ x) m
    | .call ws => .ok (callW st ws)

/-! ## Rule switches -/

structure Rules where
  /-- Reading member `m` needs every possible tag value to make `m` live,
  and the member written since the last tag write. -/
  readNeedsTag : Bool
  /-- Writing member `m` needs every possible tag value to make `m` live. -/
  writeNeedsTag : Bool
  /-- A tag write marks the union unwritten until a member write. -/
  tagWriteUnwrites : Bool
  /-- A tag test narrows the tag only; it does not mark the union written. -/
  testKeepsUnwritten : Bool
  /-- A write through a pointer, or a call, forgets the tag of every struct
  whose address has escaped. -/
  aliasKills : Bool
  /-- Member reads through untracked places are rejected. -/
  untrackedRejected : Bool
  /-- A whole-struct write (initializer, compound literal, a callee's write)
  must fill the member its tag makes live. -/
  initMatch : Bool

def Rules.full : Rules := ⟨true, true, true, true, true, true, true⟩

/-! ## Checker

Per struct: the tag values it may hold on every path here, and whether the
member for the current tag has been written since the last tag write. -/

abbrev Abs := Nat → List Nat × Bool

/-- Every possible tag makes `m` live. An empty list means the path is dead. -/
def allLive (L : Lay) (ks : List Nat) (m : Nat) : Bool := ks.all (L.live · m)

def join (A B : Abs) : Abs := fun x => ((A x).1 ++ (B x).1, (A x).2 && (B x).2)

/-- After an aliasing write the tag may be any value a whole-struct write can
give, or what it was. The written flag stays: a whole-struct write leaves
the union written, and no write leaves it as it was. -/
def kill (L : Lay) (E : List Nat) (A : Abs) : Abs :=
  fun x => if E.contains x then ((A x).1 ++ List.range L.n, (A x).2) else A x

def chk (R : Rules) (L : Lay) (E : List Nat) : Abs → Stmt → Option Abs
  | A, .skip => some A
  | A, .seq a b => (chk R L E A a).bind fun A' => chk R L E A' b
  | A, .settag x V => some (upd A x ([V], if R.tagWriteUnwrites then false else (A x).2))
  | A, .mset x m _ =>
    if !R.writeNeedsTag || allLive L (A x).1 m then some (upd A x ((A x).1, true)) else none
  | A, .mget _ x m =>
    if !R.readNeedsTag || (allLive L (A x).1 m && (A x).2) then some A else none
  | A, .ifTag x V a b =>
    let wr := if R.testKeepsUnwritten then (A x).2 else true
    (chk R L E (upd A x ((A x).1.filter (· == V), wr)) a).bind fun A1 =>
    (chk R L E (upd A x ((A x).1.filter (· != V), (A x).2)) b).bind fun A2 =>
    some (join A1 A2)
  | A, .init x V m _ =>
    if !R.initMatch || L.live V m then some (upd A x ([V], true)) else none
  | A, .addr _ x => if E.contains x then some A else none
  | A, .pset _ V m _ =>
    if !R.initMatch || L.live V m then some (if R.aliasKills then kill L E A else A) else none
  | A, .pget _ _ _ => if R.untrackedRejected then none else some A
  | A, .call ws =>
    if !R.initMatch || ws.all (fun w => L.live w.V w.m) then
      some (if R.aliasKills then kill L E A else A)
    else none

/-- Structs whose address `s` takes. -/
def addrs : Stmt → List Nat
  | .seq a b | .ifTag _ _ a b => addrs a ++ addrs b
  | .addr _ x => [x]
  | _ => []

/-- Every struct starts with tag 0 but counts as unwritten, as a local with no
initializer would: a member is readable only after an initializer or a
member write. -/
def Abs.init : Abs := fun _ => ([0], false)

/-- Check a whole program: `G` are the globals. Escape is flow-insensitive:
a struct whose address is taken anywhere is treated as escaped everywhere. -/
def check (R : Rules) (L : Lay) (G : List Nat) (s : Stmt) : Option Abs :=
  chk R L (G ++ addrs s) Abs.init s

def run (L : Lay) (G : List Nat) (s : Stmt) : Out := exec L 1000 (St.init G) s

end SafeC.Tag
