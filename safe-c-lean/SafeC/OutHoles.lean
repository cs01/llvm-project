-- Out-argument and view-array rules: for each rule, a program that passes the
-- checker with only that rule off and reaches UB; then accepted idioms under
-- the full rules. All checked by evaluation in the kernel.
import SafeC.OutSound

namespace SafeC.Out

open Stmt

def seqs : List Stmt → Stmt
  | [] => .skip
  | [s] => s
  | s :: ss => .seq s (seqs ss)

def ubOf : Out → Option UB
  | .ub k => some k
  | _ => none

def ubR (P : List Fn) : Option UB := ubOf (run P 40)

/-- Accepted with the rules `R`, rejected with all of them, and really UB. -/
def hole (R : Rules) (P : List Fn) (k : UB) : Prop :=
  check R P = true ∧ ubR P = some k ∧ check Rules.full P = false

instance (R : Rules) (P : List Fn) (k : UB) : Decidable (hole R P k) := by
  unfold hole; infer_instance

def mainFn (locals : List Ty) (b : List Stmt) : Fn := ⟨[], locals, seqs b, none⟩

/-- `void set(int **out, int *in) { *out = in; }`, `in` `_Noescape` or not. -/
def setFn (ne : Bool) : Fn := ⟨[(.out, false), (.bor, ne)], [], store 0 1, none⟩

/-! ## Each rule is necessary -/

/-- `b = malloc(); set(&o, b); free(b); *o;` (variables `b`, `o`, `i`; `i` is
the borrow of `b` that is passed). -/
def useSet (f : Nat) : Fn :=
  mainFn [.own, .bor, .bor] [new 0, null 1, get 2 0, call f [1, 2] none, free 0, deref 1]

theorem need_flowOut :
    hole { Rules.full with flowOut := false } [useSet 1, setFn false] .useAfterFree := by decide

/-- `void kept(int **out, int *in _Noescape) { *out = in; }` -/
theorem need_noEscapeChecked :
    hole { Rules.full with noEscapeChecked := false } [useSet 1, setFn true] .useAfterFree := by
  decide

/-- The same through a local: `int *l = in; *out = l;` -/
theorem need_noEscapeChecked_local :
    hole { Rules.full with noEscapeChecked := false }
      [useSet 1, ⟨[(.out, false), (.bor, true)], [.bor], seqs [copy 2 1, store 0 2], none⟩]
      .useAfterFree := by decide

/-- And through another call: `set(out, in);` -/
theorem need_noEscapeChecked_call :
    hole { Rules.full with noEscapeChecked := false }
      [useSet 1, ⟨[(.out, false), (.bor, true)], [], call 2 [0, 1] none, none⟩, setFn false]
      .useAfterFree := by decide

/-- `void set_field(int **out, struct own *s) { *out = s->p; }` -/
def setField (pt : Ty) (ne : Bool) : Fn :=
  ⟨[(.out, false), (pt, ne)], [.bor], seqs [get 2 1, store 0 2], none⟩

/-- `set_field(&o, &s); free(_Take(s.p)); *o;` -/
def useField (pt : Ty) (ne : Bool) : List Fn :=
  [mainFn [.own, .bor] [new 0, null 1, call 1 [1, 0] none, free 0, deref 1], setField pt ne]

theorem need_interior :
    hole { Rules.full with interior := false } (useField .sh false) .useAfterFree := by decide

/-- The same with `struct own *_Exclusive s`. -/
theorem need_interior_ex :
    hole { Rules.full with interior := false } (useField .ex false) .useAfterFree := by decide

/-- `struct own *s _Noescape` is exempt from `interior`, so the body must not
store `s->p` either. -/
theorem need_noEscapeChecked_field :
    hole { Rules.full with noEscapeChecked := false } (useField .sh true) .useAfterFree := by decide

/-- `void set_global(int **out) { *out = g.p; }` then
`set_global(&o); free(_Take(g.p)); *o;` -/
theorem need_noGlobalOut :
    hole { Rules.full with noGlobalOut := false }
      [mainFn [.bor] [newG, null 0, call 1 [0] none, freeG, deref 0],
       ⟨[(.out, false)], [.bor], seqs [getG 1, store 0 1], none⟩] .useAfterFree := by decide

/-- `int *get(void) { return g.p; }` then `o = get(); free(_Take(g.p)); *o;` -/
theorem need_noGlobalReturn :
    hole { Rules.full with noGlobalReturn := false }
      [mainFn [.bor] [newG, null 0, call 1 [] (some 0), freeG, deref 0],
       ⟨[], [.bor], getG 0, some 0⟩] .useAfterFree := by decide

/-- `void f(int **out) { int x; *out = &x; }` then `f(&o); *o;` -/
theorem need_noLocalOut :
    hole { Rules.full with noLocalOut := false }
      [mainFn [.bor] [null 0, call 1 [0] none, deref 0],
       ⟨[(.out, false)], [.int, .bor], seqs [addr 2 1, store 0 2], none⟩] .deadLocal := by decide

/-- `t[0].text = b; free(b); *t[0].text;` (variables `b`, `t`, `i`). -/
theorem need_viewFlow :
    hole { Rules.full with viewFlow := false }
      [mainFn [.own, .vown, .bor]
        [new 0, newv 1, get 2 0, store 1 2, free 0, load 2 1, deref 2]] .useAfterFree := by decide

/-- `q = t; q[0].text = b; free(b); *t[0].text;` -/
theorem need_viewOwnerOnly :
    hole { Rules.full with viewOwnerOnly := false }
      [mainFn [.own, .vown, .bor, .alias 1]
        [new 0, newv 1, mkalias 3, get 2 0, store 3 2, free 0, load 2 1, deref 2]]
      .useAfterFree := by decide

/-- `t[0].text = b; gtoks = _Take(t); free(b); t = _Take(gtoks); *t[0].text;` -/
theorem need_viewNoField :
    hole { Rules.full with viewNoField := false }
      [mainFn [.own, .vown, .bor]
        [new 0, newv 1, get 2 0, store 1 2, gput 1, free 0, gget 1, load 2 1, deref 2]]
      .useAfterFree := by decide

/-! ## Rules the proof needed that the list of eight does not state -/

/-- `void f(int *in, struct own *_Exclusive s) { free(_Take(s->p)); *in; }`
called as `i = s.p; f(i, &s);` -/
theorem need_argsApart :
    hole { Rules.full with argsApart := false }
      [mainFn [.own, .bor] [new 0, get 1 0, call 1 [1, 0] none],
       ⟨[(.bor, false), (.ex, false)], [], seqs [free 1, deref 0], none⟩] .useAfterFree := by decide

/-- The borrow reached through an out argument: `o = s.p; f(&o, &s);` -/
theorem need_argsApart_out :
    hole { Rules.full with argsApart := false }
      [mainFn [.own, .bor] [new 0, get 1 0, call 1 [1, 0] none],
       ⟨[(.out, false), (.ex, false)], [.bor], seqs [free 1, load 2 0, deref 2, null 2, store 0 2],
        none⟩]
      .useAfterFree := by decide

/-- The same struct twice, `f(&s, &s)`, with
`void f(struct own *a, struct own *_Exclusive s) { l = a->p; free(_Take(s->p)); *l; }` -/
theorem need_argsApart_twice :
    hole { Rules.full with argsApart := false }
      [mainFn [.own] [new 0, call 1 [0, 0] none],
       ⟨[(.sh, false), (.ex, false)], [.bor], seqs [get 2 0, free 1, deref 2], none⟩]
      .useAfterFree := by decide

/-- `void drop(void) { free(_Take(g.p)); }` then `o = g.p; drop(); *o;` -/
theorem need_callEndsGlobal :
    hole { Rules.full with callEndsGlobal := false }
      [mainFn [.bor] [newG, getG 0, call 1 [] none, deref 0], ⟨[], [], freeG, none⟩]
      .useAfterFree := by decide

/-- `void f(int **a, int **b, struct own *_Exclusive s)
{ *a = s->p; l = *b; *a = NULL; free(_Take(s->p)); *l; }` called as `f(&o, &o, &s)`. -/
theorem need_outsMayAlias :
    hole { Rules.full with outsMayAlias := false }
      [mainFn [.own, .bor] [new 0, null 1, call 1 [1, 1, 0] none],
       ⟨[(.out, false), (.out, false), (.ex, false)], [.bor, .bor],
        seqs [get 3 2, store 0 3, load 4 1, null 3, store 0 3, free 2, deref 4], none⟩]
      .useAfterFree := by decide

/-- `void f(int **out, struct own *_Exclusive s) { *out = s->p; free(_Take(s->p)); }`
then `f(&o, &s); *o;` -/
theorem need_exitLive :
    hole { Rules.full with exitLive := false }
      [mainFn [.own, .bor] [new 0, null 1, call 1 [1, 0] none, deref 1],
       ⟨[(.out, false), (.ex, false)], [.bor], seqs [get 2 1, store 0 2, free 1], none⟩]
      .useAfterFree := by decide

/-- `q = t; t[0].text = b; free(b); *q[0].text;` -/
theorem need_aliasCurrent :
    hole { Rules.full with aliasCurrent := false }
      [mainFn [.own, .vown, .bor, .alias 1]
        [new 0, newv 1, mkalias 3, get 2 0, store 1 2, free 0, load 2 3, deref 2]]
      .useAfterFree := by decide

/-! ## Idioms under the full rules -/

/-- Accepted, and this run ends normally. -/
def fine (P : List Fn) : Prop :=
  check Rules.full P = true ∧ (match run P 40 with | .ok _ => true | _ => false) = true

instance (P : List Fn) : Decidable (fine P) := by unfold fine; infer_instance

/-- `set(&o, b); *o; free(b);` -/
theorem use_first_ok :
    fine [mainFn [.own, .bor, .bor] [new 0, null 1, get 2 0, call 1 [1, 2] none, deref 1, free 0],
      setFn false] := by decide

/-- `void look(int **out, int *in _Noescape) { *out = NULL; *in; }` then
`look(&o, b); free(b);` and `o` may still be used. -/
theorem not_kept_ok :
    fine [mainFn [.own, .bor, .bor, .bor]
        [new 0, null 1, get 2 0, call 1 [1, 2] none, free 0, copy 3 1],
      ⟨[(.out, false), (.bor, true)], [.bor], seqs [null 2, store 0 2, deref 1], none⟩] := by decide

/-- `set_field(&o, &s); *o; free(_Take(s.p));` -/
theorem field_ok :
    fine [mainFn [.own, .bor] [new 0, null 1, call 1 [1, 0] none, deref 1, free 0],
      setField .sh false] := by decide

/-- `t[0].text = b; *t[0].text; free(t); free(b);` -/
theorem views_ok :
    fine [mainFn [.own, .vown, .bor]
        [new 0, newv 1, get 2 0, store 1 2, load 2 1, deref 2, free 1, free 0]] := by decide

/-- `set(t, b)`: a whole view written through the callee's parameter. -/
theorem fill_ok :
    fine [mainFn [.own, .vown, .bor]
        [new 0, newv 1, get 2 0, call 1 [1, 2] none, load 2 1, deref 2, free 1, free 0],
      setFn false] := by decide

/-- Freeing `b` before reading `t` is rejected. -/
theorem fill_rejected :
    check Rules.full [mainFn [.own, .vown, .bor]
        [new 0, newv 1, get 2 0, call 1 [1, 2] none, free 0, load 2 1],
      setFn false] = false := by decide

/-- Reading through an alias: `q = t; t[0].text = b; *q[0].text;` -/
theorem alias_read_ok :
    fine [mainFn [.own, .vown, .bor, .alias 1]
        [new 0, newv 1, mkalias 3, get 2 0, store 1 2, load 2 3, deref 2, free 1, free 0]] := by
  decide

/-- A result that borrows an argument: `o = id(i); *o; free(b);` -/
theorem id_ok :
    fine [mainFn [.own, .bor, .bor] [new 0, get 1 0, call 1 [1] (some 2), deref 2, free 0],
      ⟨[(.bor, false)], [], skip, some 0⟩] := by decide

/-- Relaying an out parameter: `void relay(int **out, int *in) { set(out, in); }` -/
theorem relay_ok :
    fine [mainFn [.own, .bor, .bor] [new 0, null 1, get 2 0, call 1 [1, 2] none, deref 1, free 0],
      ⟨[(.out, false), (.bor, false)], [], call 2 [0, 1] none, none⟩, setFn false] := by decide

/-- The theorem applies to each accepted program, for all fuel; here to the
last one. -/
theorem relay_sound (fuel : Nat) :
    (run [mainFn [.own, .bor, .bor] [new 0, null 1, get 2 0, call 1 [1, 2] none, deref 1, free 0],
      ⟨[(.out, false), (.bor, false)], [], call 2 [0, 1] none, none⟩, setFn false] fuel).isUB
      = false :=
  out_sound _ relay_ok.1 fuel

end SafeC.Out
