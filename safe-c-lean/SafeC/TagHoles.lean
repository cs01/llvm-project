-- Tagged-union rules: for each rule, a program that passes the checker with
-- that rule off and reads a member as another type; then accepted idioms under
-- the full rules. All checked by evaluation in the kernel.
import SafeC.TagSound

namespace SafeC.Tag

open Stmt

def seqs : List Stmt → Stmt
  | [] => .skip
  | [s] => s
  | s :: ss => .seq s (seqs ss)

def ok (R : Rules) (L : Lay) (G : List Nat) (s : Stmt) : Bool := (check R L G s).isSome

def conf (o : Out) : Bool := o.isUB

/-- `struct T { enum K k; union _Tagged_by(k) { long i _When(I); int *p _When(P); }; }`
from safe-c-tagged-holes.c, with `I = 0`, `P = 1`. Struct 0 is `t`, scalar 0
is `y`, pointer 0 is `q`. -/
def LT : Lay := ⟨[.int, .ptr], [0, 1]⟩

/-- An integer whose bytes would be read as a pointer. -/
def X : Int := 0x41414141

/-! ## Each rule is necessary -/

/-- `t = {.k = I, .i = X}; y = t.p;` -/
def cRead : Stmt := seqs [init 0 0 0 X, mget 0 0 1]
theorem need_readNeedsTag :
    ok { Rules.full with readNeedsTag := false } LT [] cRead = true ∧
    conf (run LT [] cRead) = true ∧ ok Rules.full LT [] cRead = false := by decide

/-- `t = {.k = P, .p = &g}; t.i = X; y = t.p;` -/
def cWrite : Stmt := seqs [init 0 1 1 5, mset 0 0 X, mget 0 0 1]
theorem need_writeNeedsTag :
    ok { Rules.full with writeNeedsTag := false } LT [] cWrite = true ∧
    conf (run LT [] cWrite) = true ∧ ok Rules.full LT [] cWrite = false := by decide

/-- Hole 06: `t = {.k = I, .i = 0}; t.i = X; t.k = P; y = t.p;` -/
def c06 : Stmt := seqs [init 0 0 0 0, mset 0 0 X, settag 0 1, mget 0 0 1]
theorem need_tagWriteUnwrites :
    ok { Rules.full with tagWriteUnwrites := false } LT [] c06 = true ∧
    conf (run LT [] c06) = true ∧ ok Rules.full LT [] c06 = false := by decide

/-- Hole 06 behind a test: `t = {.k = I, .i = X}; t.k = P; if (t.k == P) y = t.p;`.
A test that sets the tag fact afresh must keep the unwritten mark. -/
def c06t : Stmt := seqs [init 0 0 0 X, settag 0 1, ifTag 0 1 (mget 0 0 1) skip]
theorem need_testKeepsUnwritten :
    ok { Rules.full with testKeepsUnwritten := false } LT [] c06t = true ∧
    conf (run LT [] c06t) = true ∧ ok Rules.full LT [] c06t = false := by decide

/-- Write through an alias: `t = {.k = P, .p = &g}; q = &t; *q = (struct T){.k = I, .i = X}; y = t.p;` -/
def cAlias : Stmt := seqs [init 0 1 1 5, addr 0 0, pset 0 0 0 X, mget 0 0 1]
theorem need_aliasKills_ptr :
    ok { Rules.full with aliasKills := false } LT [] cAlias = true ∧
    conf (run LT [] cAlias) = true ∧ ok Rules.full LT [] cAlias = false := by decide

/-- Hole 14: `TG` is a global; `rt()` sets it to `{.k = I, .i = X}`.
`TG = {.k = P, .p = &g}; if (TG.k == P) { rt(); y = TG.p; }` -/
def c14 : Stmt := seqs [init 0 1 1 5, ifTag 0 1 (seqs [call [⟨0, 0, 0, X⟩], mget 0 0 1]) skip]
theorem need_aliasKills_call :
    ok { Rules.full with aliasKills := false } LT [0] c14 = true ∧
    conf (run LT [0] c14) = true ∧ ok Rules.full LT [0] c14 = false := by decide

/-- Hole 07: a read through a place whose tag is not followed:
`t = {.k = I, .i = X}; q = &t; y = q->p;` (in the C test, `a[j]`). -/
def c07 : Stmt := seqs [init 0 0 0 X, addr 0 0, pget 0 0 1]
theorem need_untrackedRejected :
    ok { Rules.full with untrackedRejected := false } LT [] c07 = true ∧
    conf (run LT [] c07) = true ∧ ok Rules.full LT [] c07 = false := by decide

/-- Hole 12: `t = {.k = P, .i = X}; if (t.k == P) y = t.p;` -/
def c12 : Stmt := seqs [init 0 1 0 X, ifTag 0 1 (mget 0 0 1) skip]
theorem need_initMatch :
    ok { Rules.full with initMatch := false } LT [] c12 = true ∧
    conf (run LT [] c12) = true ∧ ok Rules.full LT [] c12 = false := by decide

/-- Hole 12 in a callee: `rt()` sets `TG = {.k = P, .i = X}`. The callee is
checked by the same rule. -/
def c12c : Stmt := seqs [init 0 1 1 5, call [⟨0, 1, 0, X⟩], ifTag 0 1 (mget 0 0 1) skip]
theorem need_initMatch_call :
    ok { Rules.full with initMatch := false } LT [0] c12c = true ∧
    conf (run LT [0] c12c) = true ∧ ok Rules.full LT [0] c12c = false := by decide

/-! ## Idioms under the full rules -/

/-- `struct shape { int kind; union _Tagged_by(kind) { double radius _When(0);
struct {int w, h;} rect _When(1); int *p _When(2); }; }`. The rect is one
cell of type int here. -/
def LS : Lay := ⟨[.dbl, .int, .ptr], [0, 1, 2]⟩

/-- A switch over every variant, the last one as `default`: the default arm
knows the tag is 2 because the cases took 0 and 1. -/
def cSwitch : Stmt := seqs [init 0 2 2 7,
  switchS 0 [(0, mget 0 0 0), (1, mget 0 0 1)] (mget 0 0 2)]
theorem switch_ok : ok Rules.full LS [] cSwitch = true := by decide

/-- After a call that may rewrite a global shape, the switch still works: the
tag is one of 0, 1, 2 and each arm narrows it. -/
def cSwitchCall : Stmt := seqs [init 0 1 1 4, call [⟨0, 2, 2, 7⟩],
  switchS 0 [(0, mget 0 0 0), (1, mget 0 0 1), (2, mget 0 0 2)] skip]
theorem switch_after_call_ok : ok Rules.full LS [0] cSwitchCall = true := by decide

/-- Missing case 1 and reading `p` in `default` is rejected once the tag is
unknown. -/
def cSwitchMiss : Stmt := seqs [init 0 2 2 7, call [⟨0, 1, 1, 4⟩],
  switchS 0 [(0, mget 0 0 0)] (mget 0 0 2)]
theorem switch_miss_rejected : ok Rules.full LS [0] cSwitchMiss = false := by decide

/-- Tag write, then member write, then read (h06_ok). -/
def cTagThen : Stmt := seqs [init 0 0 0 1, settag 0 2, mset 0 2 5, mget 0 0 2]
theorem tag_then_member_ok : ok Rules.full LS [] cTagThen = true := by decide

/-- `SAFEC_RESULT`: a `bool ok` tag, `err` (int) for `false`, `val` (a pointer)
for `true`. `r = ok(p); r = f();` where `f` may write `r` (passed by
address), then `if (r.ok) use(r.val) else use(r.err)`. The `else` arm knows
`ok` is false. -/
def LR : Lay := ⟨[.int, .ptr], [0, 1]⟩
def cResult : Stmt := seqs [init 0 1 1 5, addr 0 0, call [⟨0, 0, 0, 3⟩],
  ifTag 0 1 (mget 0 0 1) (mget 1 0 0)]
theorem result_ok : ok Rules.full LR [] cResult = true := by decide

/-- A local whose address is never taken keeps its tag across a call and a
write through a pointer to another struct. -/
def cLocal : Stmt := seqs [init 1 1 1 5, addr 0 0, init 0 0 0 0, pset 0 1 1 6,
  call [⟨0, 0, 0, X⟩], mget 0 1 1]
theorem local_survives_ok : ok Rules.full LT [] cLocal = true := by decide

/-! ## The idioms are safe for every amount of fuel -/

theorem accepted_sound (L : Lay) (G : List Nat) (s : Stmt) (h : ok Rules.full L G s = true)
    (fuel : Nat) : (exec L fuel (St.init G) s).isUB = false := by
  obtain ⟨A, hA⟩ := Option.isSome_iff_exists.mp h
  exact tag_sound L G s A hA fuel

theorem idioms_sound (fuel : Nat) :
    (exec LS fuel (St.init []) cSwitch).isUB = false ∧
    (exec LS fuel (St.init [0]) cSwitchCall).isUB = false ∧
    (exec LS fuel (St.init []) cTagThen).isUB = false ∧
    (exec LR fuel (St.init []) cResult).isUB = false ∧
    (exec LT fuel (St.init []) cLocal).isUB = false :=
  ⟨accepted_sound _ _ _ switch_ok fuel, accepted_sound _ _ _ switch_after_call_ok fuel,
   accepted_sound _ _ _ tag_then_member_ok fuel, accepted_sound _ _ _ result_ok fuel,
   accepted_sound _ _ _ local_survives_ok fuel⟩

end SafeC.Tag
