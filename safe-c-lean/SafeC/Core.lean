-- The core subset (notes/core-subset.md, "The rules to prove"): syntax, semantics, checker.
namespace SafeC.Core

inductive Mode | excl | shr deriving DecidableEq, Repr

/-- Parameter kinds: moved in, moved in only if the result is true, exclusive, const. -/
inductive PK | own | succ | excl | shr deriving DecidableEq, Repr

/-- Result types. `bor i`: a borrow inside parameter `i` (rule 9). -/
inductive RT | unit | bool | own | bor (i : Nat) deriving DecidableEq, Repr

inductive RExp | unit | lit (v : Bool) | bvar (b : Nat) | ptr (x : Nat) deriving DecidableEq, Repr

inductive Dest | none | b (b : Nat) | p (x : Nat) deriving DecidableEq, Repr

/-- Pointer locals and boolean locals are two separate name spaces.
Locals `0 .. params.length - 1` of a function are its parameters. -/
inductive Cmd
  | skip
  | new (x : Nat)
  | nil (x : Nat)                             -- x = null owner
  | fin (x : Nat)                             -- x goes out of scope
  | borrow (x : Nat) (m : Mode) (v : Nat)     -- x = &*v
  | step (x : Nat) (m : Mode) (v f : Nat)     -- x = &*v->f
  | read (b v : Nat)                          -- b = some data of *v
  | test (b v : Nat)                          -- b = (v != null)
  | havoc (b : Nat)
  | setb (b : Nat) (v : Bool)
  | store (v f o : Nat)                       -- v->f = move o
  | take (x v f : Nat)                        -- x = take(v->f)
  | mv (x o : Nat)
  | free (o : Nat)
  | seq (c d : Cmd)
  | ite (b : Nat) (c d : Cmd)
  | while (b : Nat) (c : Cmd)
  | call (d : Dest) (m : Mode) (f : Nat) (args : List Nat)  -- m: how the caller holds a borrow result
  | ret (r : RExp)
  deriving Repr

structure Fn where
  params : List PK
  rt : RT
  nlocals : Nat        -- only bounds the loop fixpoint
  body : Cmd

abbrev Prog := List Fn

/-! ## Semantics -/

structure Heap where
  next : Nat
  alive : Nat → Bool
  fld : Nat → Nat → Option Nat

def Heap.empty : Heap := ⟨0, fun _ => false, fun _ _ => none⟩

inductive RV | unit | b (v : Bool) | p (v : Option Nat)

structure S where
  h : Heap
  env : Nat → Option Nat
  bs : Nat → Bool
  orc : Nat → Bool     -- nondeterminism: the next answers

/-- `err`: use after free, double free, free of a non-object, falling off a body. -/
inductive Out | ok (s : S) | ret (s : S) (v : RV) | trap | oof | err

def Out.isErr : Out → Bool | .err => true | _ => false

def upd {α} (g : Nat → α) (x : Nat) (v : α) : Nat → α := fun y => if y = x then v else g y

def Heap.alloc (h : Heap) : Heap := ⟨h.next + 1, upd h.alive h.next true, h.fld⟩
def Heap.setf (h : Heap) (a f : Nat) (v : Option Nat) : Heap :=
  { h with fld := upd h.fld a (upd (h.fld a) f v) }
def Heap.kill (h : Heap) (a : Nat) : Heap := { h with alive := upd h.alive a false }

def S.setp (s : S) (x : Nat) (v : Option Nat) : S := { s with env := upd s.env x v }
def S.setb (s : S) (b : Nat) (v : Bool) : S := { s with bs := upd s.bs b v }
def S.ask (s : S) (b : Nat) : S :=
  { s with bs := upd s.bs b (s.orc 0), orc := fun n => s.orc (n + 1) }

/-- Dereference: null traps, a dead object is an error. -/
def deref (s : S) (v : Nat) (k : Nat → Out) : Out :=
  match s.env v with
  | none => .trap
  | some a => if s.h.alive a then k a else .err

def argv (env : Nat → Option Nat) (args : List Nat) (i : Nat) : Option Nat :=
  match args[i]? with | some v => env v | none => none

def assign (d : Dest) (v : RV) (s : S) : S :=
  match d, v with
  | .b b, .b v => s.setb b v
  | .p x, .p v => s.setp x v
  | _, _ => s

def retVal (s : S) : RExp → RV
  | .unit => .unit | .lit v => .b v | .bvar b => .b (s.bs b) | .ptr x => .p (s.env x)

def exec (P : Prog) : Nat → Cmd → S → Out
  | 0, _, _ => .oof
  | n + 1, c, s =>
    match c with
    | .skip => .ok s
    | .new x => .ok ({ s with h := s.h.alloc }.setp x (some s.h.next))
    | .nil x => .ok (s.setp x none)
    | .fin x => .ok (s.setp x none)
    | .borrow x _ v => .ok (s.setp x (s.env v))
    | .step x _ v f => deref s v fun a => .ok (s.setp x (s.h.fld a f))
    | .read b v => deref s v fun _ => .ok (s.ask b)
    | .test b v => .ok (s.setb b (s.env v).isSome)
    | .havoc b => .ok (s.ask b)
    | .setb b v => .ok (s.setb b v)
    | .store v f o => deref s v fun a => .ok { s with h := s.h.setf a f (s.env o) }
    | .take x v f => deref s v fun a =>
        .ok ({ s with h := s.h.setf a f none }.setp x (s.h.fld a f))
    | .mv x o => .ok (s.setp x (s.env o))
    | .free o =>
      match s.env o with
      | none => .ok s
      | some a => if s.h.alive a then .ok { s with h := s.h.kill a } else .err
    | .seq c d =>
      match exec P n c s with
      | .ok s' => exec P n d s'
      | o => o
    | .ite b c d => if s.bs b then exec P n c s else exec P n d s
    | .while b c =>
      if s.bs b then
        match exec P n c s with
        | .ok s' => exec P n (.while b c) s'
        | o => o
      else .ok s
    | .call d _ f args =>
      match P[f]? with
      | none => .err
      | some fn =>
        match exec P n fn.body ⟨s.h, argv s.env args, fun _ => false, s.orc⟩ with
        | .ret s' v => .ok (assign d v { s with h := s'.h, orc := s'.orc })
        | .ok _ => .err
        | o => o
    | .ret r => .ret s (retVal s r)

/-- Run function `f` as main: no arguments, empty heap, any oracle. -/
def run (P : Prog) (f fuel : Nat) (orc : Nat → Bool) : Out :=
  exec P fuel (.call .none .shr f []) ⟨.empty, fun _ => none, fun _ => false, orc⟩

/-! ## Checker -/

/-- What the checker knows of a local. `maybe b`: still owned iff boolean `b` is false.
`bor c anc`: a borrow, capable of mutation iff `c`; `anc` is the ancestors, parent first, root last,
empty for a borrowed parameter, which is its own root. Re-parenting the children of a local is
removing it from every ancestor list. -/
inductive K | dead | own | maybe (b : Nat) | bor (c : Bool) (anc : List Nat)
  deriving DecidableEq, Repr

abbrev St := Nat → K

def K.path : K → List Nat | .bor _ p => p | _ => []
def K.lv : K → Bool | .own => true | .bor _ _ => true | _ => false
def K.ex : K → Bool | .own => true | .bor c _ => c | _ => false

def rootOf (g : St) (x : Nat) : Nat := (g x).path.getLastD x
def clo (g : St) (x : Nat) : List Nat := x :: (g x).path

/-- One switch per rule, for the necessity checks. -/
structure Rules where
  killMut := true       -- mutating through v kills the rest of v's root, except its ancestors
  moveKills := true     -- a moved or freed owner is dead, with its descendants
  needCap := true       -- mutation needs a capable local
  capInherit := true    -- a borrow is capable only if its parent is
  reparent := true      -- an assigned local leaves every ancestor list
  roots := true         -- no shared root among arguments unless all const; no store under itself
  retThrough := true    -- rule 9: the result was reached through the named parameter
  resCap := true        -- rule 9: the result is capable only if the argument is
  resChild := true      -- rule 9: the result is a child of the argument (else a root of its own)
  heldOnFalse := true   -- rule 10: parameter held live when returning false
  movedOnTrue := true   -- rule 10: parameter not live when returning true
  maybeTied := true     -- rule 10: maybe-moved until the result is tested (else still owned)
  untie := true         -- rule 10: assigning the boolean ends the tie
  noParamAssign := true -- added: a parameter is never assigned

def Rules.full (movedOnTrue : Bool) : Rules := { movedOnTrue := movedOnTrue }

/-- A borrow lives only while its root does. -/
def prune (g : St) : St := fun x =>
  match g x with
  | .bor c (p :: ps) =>
    let r := ps.getLastD p
    if (g r).lv && (g r).path.isEmpty && (!c || (g r).ex) then .bor c (p :: ps) else .dead
  | k => k

/-- Remove `x` from an ancestor list. -/
def shrink (q : Nat → Bool) : K → K
  | .bor c (p :: ps) =>
    match (p :: ps).filter q with
    | [] => .dead
    | l => .bor c l
  | k => k

/-- `x` is about to be assigned: its children become its parent's children, or die if it is a root. -/
def drop (R : Rules) (x : Nat) (g : St) : St :=
  prune fun y => if y = x then .dead else shrink (fun z => !R.reparent || z != x) (g y)

/-- Mutation through `v`: the live locals of v's root that survive are v and its ancestors. -/
def hit (g : St) (v x : Nat) : Bool := rootOf g x == rootOf g v && !(clo g v).contains x

def mutate (R : Rules) (v : Nat) (g : St) : St :=
  if R.killMut then prune fun x => if hit g v x then .dead else g x else g

def moveOut (R : Rules) (o : Nat) (g : St) : St :=
  if R.moveKills then prune (upd g o .dead) else g

def untie (R : Rules) (b : Nat) (g : St) : St :=
  prune fun x => if R.untie && g x == .maybe b then .dead else g x

/-- The state on the branch where `b` is `v`. -/
def assume (b : Nat) (v : Bool) (g : St) : St :=
  fun x => if g x = .maybe b then (if v then .dead else .own) else g x

def joinK (k l : K) : K :=
  match k, l with
  | .bor c p, .bor c' p' =>
    if p.isEmpty != p'.isEmpty then .dead else shrink (fun z => p'.contains z) (.bor (c && c') p)
  | k, l => if k = l then k else .dead

/-- Common ancestors only; dead if there is none. -/
def join (g h : St) : St := prune fun x => joinK (g x) (h x)

/-- `none` is an unreachable program point. -/
def joinR : Option St → Option St → Option St
  | none, r => r
  | r, none => r
  | some g, some h => some (join g h)

def grd (b : Bool) (r : Option α) : Option α := if b then r else none

def trunc (n : Nat) (g : St) : St :=
  let l := (List.range n).map g
  prune fun x => l.getD x .dead

/-- `k` claims no more than `l`. -/
def kle (k l : K) : Bool :=
  match k, l with
  | .dead, _ => true
  | .bor c p, .bor c' p' => (!c || c') && p.all p'.contains && p.isEmpty == p'.isEmpty
  | k, l => k = l

def stLe (n : Nat) (h g : St) : Bool := (List.range n).all fun x => kle (h x) (g x)

/-- Weaken by joins until the body maps the state to a stronger one. -/
def loopHead (n : Nat) (body : St → Option (Option St)) : Nat → St → Option St
  | 0, g => some g
  | k + 1, g =>
    match body g with
    | none => none
    | some none => some g
    | some (some g') => if stLe n g g' then some g else loopHead n body k (trunc n (join g g'))

def pOk (R : Rules) (np x : Nat) : Bool := !R.noParamAssign || decide (np ≤ x)

/-- `x` becomes a borrow reached through `v` (`x = v` keeps the parent). -/
def borrowK (R : Rules) (np x : Nat) (m : Mode) (v : Nat) (g : St) : Option St :=
  let p := (if x = v then [] else [v]) ++ (g v).path.filter (fun z => !R.reparent || z != x)
  grd (pOk R np x && (g v).lv && rootOf g v != x && !p.isEmpty)
    (some (upd (drop R x g) x (.bor (m == .excl && (!R.capInherit || (g v).ex)) p)))

/-- The root test never fails on reachable states; it saves the proof an invariant. -/
def mutOk (R : Rules) (g : St) (v : Nat) : Bool :=
  if R.needCap then (g v).ex && (g (rootOf g v)).lv else (g v).lv

def argOk (R : Rules) (g : St) (v : Nat) : PK → Bool
  | .own | .succ => g v = .own
  | .excl => mutOk R g v
  | .shr => (g v).lv

/-- No two arguments share a root, unless both are const. -/
def rootsOk (g : St) (az : List (Nat × PK)) : Bool :=
  (List.range az.length).all fun i => (List.range az.length).all fun j =>
    match az[i]?, az[j]? with
    | some (v, k), some (w, l) => i == j || (k == .shr && l == .shr) || rootOf g v != rootOf g w
    | _, _ => true

/-- The caller's state of an argument taken on success. -/
def succK : Dest → K | .b b => .maybe b | _ => .dead

/-- The caller's state after the arguments are passed; `d` is where the result goes. -/
def argSt (R : Rules) (d : Dest) (az : List (Nat × PK)) (g : St) : St :=
  prune fun x =>
    if R.moveKills && az.contains (x, .own) then .dead
    else if R.maybeTied && az.contains (x, .succ) then succK d
    else if R.killMut && az.any (fun q => q.2 == .excl && hit g q.1 x) then .dead
    else g x

def assignK (R : Rules) (np : Nat) (d : Dest) (m : Mode) (rt : RT) (args : List Nat) (g : St) : Option St :=
  match d, rt with
  | .none, _ => some g
  | .b _, .bool => some g
  | .p x, .own => grd (pOk R np x) (some (upd (drop R x g) x .own))
  | .p x, .bor i =>
    match args[i]? with
    | some v =>
      if R.resChild then borrowK { R with capInherit := R.resCap } np x m v g
      else grd (pOk R np x) (some (upd (drop R x g) x (.bor (m == .excl) [])))
    | none => none
  | _, _ => none

def untieD (R : Rules) (d : Dest) (g : St) : St :=
  match d with | .b b => untie R b g | _ => g

def callK (R : Rules) (P : Prog) (np : Nat) (d : Dest) (m : Mode) (f : Nat) (args : List Nat) (g : St) :
    Option St :=
  match P[f]? with
  | none => none
  | some fn =>
    let az := args.zip fn.params
    grd (decide (args.length = fn.params.length) && az.all (fun q => argOk R g q.1 q.2) &&
        (!R.roots || rootsOk g az))
      (assignK R np d m fn.rt args (argSt R d az (untieD R d g)))

/-- Conditions on parameter `i` at a return. -/
def retParam (R : Rules) (g : St) (r : RExp) (i : Nat) : PK → Bool
  | .excl => g i = .bor true []
  | .succ =>
    match r with
    | .lit true => !R.movedOnTrue || g i != .own
    | .lit false => !R.heldOnFalse || g i == .own
    | .bvar b => !R.heldOnFalse || g i == .maybe b
    | _ => false
  | _ => true

def retParams (R : Rules) (g : St) (r : RExp) (ps : List PK) : Bool :=
  (List.range ps.length).all fun i =>
    match ps[i]? with | some k => retParam R g r i k | none => true

def retVOk (R : Rules) (g : St) (rt : RT) (r : RExp) : Bool :=
  match rt, r with
  | .unit, .unit => true
  | .bool, .lit _ => true
  | .bool, .bvar _ => true
  | .own, .ptr x => g x = .own
  | .bor i, .ptr x => (g x).lv && (!R.retThrough || (clo g x).contains i)
  | _, _ => false

def chk (R : Rules) (P : Prog) (fn : Fn) : Cmd → St → Option (Option St)
  | .skip, g => some (some g)
  | .new x, g => grd (pOk R fn.params.length x) (some (some (upd (drop R x g) x .own)))
  | .nil x, g => grd (pOk R fn.params.length x) (some (some (upd (drop R x g) x .own)))
  | .fin x, g => grd (pOk R fn.params.length x) (some (some (drop R x g)))
  | .borrow x m v, g => (borrowK R fn.params.length x m v g).map some
  | .step x m v _, g => (borrowK R fn.params.length x m v g).map some
  | .read b v, g => grd (g v).lv (some (some (untie R b g)))
  | .test b v, g => grd (g v).lv (some (some (untie R b g)))
  | .havoc b, g => some (some (untie R b g))
  | .setb b _, g => some (some (untie R b g))
  | .store v _ o, g =>
    grd (g o = .own && mutOk R g v && (!R.roots || rootOf g v != o))
      (some (some (moveOut R o (mutate R v g))))
  | .take x v _, g =>
    grd (pOk R fn.params.length x && mutOk R g v)
      (some (some (upd (drop R x (mutate R v g)) x .own)))
  | .mv x o, g =>
    grd (pOk R fn.params.length x && g o = .own) (some (some (upd (drop R x (moveOut R o g)) x .own)))
  | .free o, g => grd (g o = .own) (some (some (moveOut R o g)))
  | .seq c d, g =>
    match chk R P fn c g with
    | none => none
    | some none => some none
    | some (some g') => chk R P fn d g'
  | .ite b c d, g =>
    match chk R P fn c (assume b true g), chk R P fn d (assume b false g) with
    | some r1, some r2 => some (joinR r1 r2)
    | _, _ => none
  | .while b c, g =>
    match loopHead fn.nlocals (fun g => chk R P fn c (assume b true g)) (4 * fn.nlocals)
        (trunc fn.nlocals g) with
    | none => none
    | some g0 =>
      let h := trunc fn.nlocals g0
      match chk R P fn c (assume b true h) with
      | none => none
      | some r =>
        grd (stLe fn.nlocals h g && (match r with | none => true | some g' => stLe fn.nlocals h g'))
          (some (some (assume b false h)))
  | .call d m f args, g => (callK R P fn.params.length d m f args g).map some
  | .ret r, g => grd (retParams R g r fn.params && retVOk R g fn.rt r) (some none)

def initK : PK → K | .own | .succ => .own | .excl => .bor true [] | .shr => .bor false []

def init (fn : Fn) : St := fun i => match fn.params[i]? with | some k => initK k | none => .dead

def fnShape (fn : Fn) : Bool :=
  (!fn.params.contains .succ || fn.rt == .bool) &&
  (match fn.rt with
   | .bor i => fn.params[i]? == some .shr || fn.params[i]? == some .excl
   | _ => true)

def chkFn (R : Rules) (P : Prog) (fn : Fn) : Bool :=
  fnShape fn && (match chk R P fn fn.body (init fn) with | some none => true | _ => false)

def checkProg (R : Rules) (P : Prog) : Bool := P.all (chkFn R P)

def check (P : Prog) : Bool := checkProg (Rules.full true) P

end SafeC.Core
