-- The core subset: each rule is needed, and the idioms that pass.
import SafeC.Core
namespace SafeC.Core

open Cmd

def sq : List Cmd → Cmd
  | [] => skip
  | [c] => c
  | c :: cs => .seq c (sq cs)

def allFalse : Nat → Bool := fun _ => false
def allTrue : Nat → Bool := fun _ => true

/-- Accepted without the rule, rejected with it, and reaches a use after free or double free. -/
structure Needed (R : Rules) (P : Prog) : Prop where
  without : checkProg R P = true
  withRule : check P = false
  err : (run P 0 40 allFalse).isErr = true

def unitFn (ps : List PK) (n : Nat) (cs : List Cmd) : Fn := ⟨ps, .unit, n, sq (cs ++ [ret .unit])⟩

/-- `o -> p` with `o` in local 0 and the owner field `o->0` set. -/
def mk2 : List Cmd := [new 0, new 1, store 0 0 1]

/-! ## Rules -/

/-- A descendant of the mutated local dies. -/
theorem need_killMut : Needed { killMut := false }
    [unitFn [] 4 (mk2 ++ [step 2 .shr 0 0, take 3 0 0, free 3, read 0 2])] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- A sibling of the mutated local dies. -/
theorem need_killMut_sibling : Needed { killMut := false }
    [unitFn [] 5 (mk2 ++ [borrow 2 .excl 0, step 3 .shr 0 0, take 4 2 0, free 4, read 0 3])] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- A cousin of the mutated local dies. -/
theorem need_killMut_cousin : Needed { killMut := false }
    [unitFn [] 6 (mk2 ++ [borrow 2 .excl 0, borrow 5 .excl 0, step 3 .shr 5 0, take 4 2 0, free 4, read 0 3])] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

theorem need_moveKills : Needed { moveKills := false } [unitFn [] 1 [new 0, free 0, free 0]] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- A local that is not capable cannot mutate. -/
theorem need_needCap : Needed { needCap := false }
    [unitFn [] 3 (mk2 ++ [step 2 .shr 0 0, call .none .shr 1 [0], read 0 2]),
     unitFn [.shr] 2 [take 1 0 0, free 1]] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- A borrow of a const borrow is not capable. -/
theorem need_capInherit : Needed { capInherit := false }
    [unitFn [] 3 (mk2 ++ [step 2 .shr 0 0, call .none .shr 1 [0], read 0 2]),
     unitFn [.shr] 3 [borrow 1 .excl 0, take 2 1 0, free 2]] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- `c = &o; y = &*c; c = c->f;` then `c` is not an ancestor of `y` any more. -/
theorem need_reparent : Needed { reparent := false }
    [unitFn [] 4 (mk2 ++ [borrow 1 .excl 0, borrow 2 .excl 1, step 1 .excl 1 0, take 3 2 0, free 3, read 0 1])] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- Two exclusive arguments with one root. -/
theorem need_roots : Needed { roots := false }
    [unitFn [] 3 (mk2 ++ [step 2 .excl 0 0, call .none .shr 1 [0, 2]]),
     unitFn [.excl, .excl] 3 [take 2 0 0, free 2, read 0 1]] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- An exclusive and a const argument with one root. -/
theorem need_roots_const : Needed { roots := false }
    [unitFn [] 3 (mk2 ++ [step 2 .shr 0 0, call .none .shr 1 [0, 2]]),
     unitFn [.excl, .shr] 3 [take 2 0 0, free 2, read 0 1]] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- An owned and a const argument with one root. -/
theorem need_roots_own : Needed { roots := false }
    [unitFn [] 1 [new 0, call .none .shr 1 [0, 0]],
     unitFn [.own, .shr] 2 [free 0, read 0 1]] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-! ## Rule 9 -/

/-- Only what is reached through the named parameter may be returned. -/
theorem need_retThrough : Needed { retThrough := false }
    [unitFn [] 3 [new 0, new 1, call (.p 2) .shr 1 [0, 1], free 1, read 0 2],
     ⟨[.shr, .shr], .bor 0, 2, ret (.ptr 1)⟩] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

def idFn : Fn := ⟨[.shr], .bor 0, 1, ret (.ptr 0)⟩

/-- The result is capable only if the argument is. -/
theorem need_resCap : Needed { resCap := false }
    [unitFn [] 3 (mk2 ++ [step 2 .shr 0 0, call .none .shr 1 [0], read 0 2]),
     unitFn [.shr] 3 [call (.p 1) .excl 2 [0], take 2 1 0, free 2],
     idFn] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- The result dies with the argument. -/
theorem need_resChild : Needed { resChild := false }
    [unitFn [] 2 [new 0, call (.p 1) .shr 1 [0], free 0, read 0 1], idFn] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-! ## Rule 10 -/

/-- The body still holds the parameter when it returns false. -/
theorem need_heldOnFalse : Needed { heldOnFalse := false }
    [unitFn [] 1 [new 0, call (.b 0) .shr 1 [0], ite 0 skip (free 0)],
     ⟨[.succ], .bool, 1, sq [free 0, ret (.lit false)]⟩] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

def setFn : Fn := ⟨[.excl, .succ], .bool, 2, sq [store 0 0 1, ret (.lit true)]⟩

/-- The owner is maybe moved until the result is tested. -/
theorem need_maybeTied : Needed { maybeTied := false }
    [unitFn [] 3 [new 0, new 1, call (.b 0) .shr 1 [0, 1], free 1, step 2 .shr 0 0, read 0 2], setFn] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- Assigning the boolean ends the tie. -/
theorem need_untie : Needed { untie := false }
    [unitFn [] 3 [new 0, new 1, call (.b 0) .shr 1 [0, 1], setb 0 false, ite 0 skip (free 1),
      step 2 .shr 0 0, read 0 2], setFn] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- HOLE in the rules as written: "must still hold it live" is not enough, it must be the caller's
object. The body frees the parameter, assigns a new object to it, and returns false. -/
theorem hole_succ_reassigned : Needed { noParamAssign := false }
    [unitFn [] 1 [new 0, call (.b 0) .shr 1 [0], ite 0 skip (free 0)],
     ⟨[.succ], .bool, 1, sq [free 0, new 0, ret (.lit false)]⟩] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- The same hole through rule 9: the result is reached through the parameter's local, which no longer
points into the argument. -/
theorem hole_param_reassigned : Needed { noParamAssign := false }
    [unitFn [] 3 [new 0, new 1, call (.p 2) .shr 1 [0, 1], free 1, read 0 2],
     ⟨[.shr, .shr], .bor 0, 2, sq [borrow 0 .shr 1, ret (.ptr 0)]⟩] :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-- `movedOnTrue` is about leaks only: `core_sound` holds without it, and this leaks. -/
theorem movedOnTrue_leaks :
    let P := [unitFn [] 1 [new 0, call (.b 0) .shr 1 [0], ite 0 skip (free 0)],
      ⟨[.succ], .bool, 1, ret (.lit true)⟩]
    check P = false ∧ checkProg (Rules.full false) P = true ∧
      (match run P 0 40 allFalse with | .ok s => s.h.alive 0 | _ => false) = true :=
  ⟨by decide +kernel, by decide +kernel, by decide +kernel⟩

/-! ## Accepted -/

def runsOk (P : Prog) (orc : Nat → Bool) : Bool := match run P 0 60 orc with | .ok _ => true | _ => false

def Accepted (P : Prog) : Prop := check P = true ∧ runsOk P allFalse = true ∧ runsOk P allTrue = true

/-- `T *get(const S *s) { return s->f; }` -/
def getFn : Fn := ⟨[.shr], .bor 0, 2, sq [step 1 .shr 0 0, ret (.ptr 1)]⟩

/-- A getter whose result is written through, then read through. -/
theorem ok_getter : Accepted
    [unitFn [] 5 (mk2 ++ [call (.p 2) .excl 1 [0], new 3, store 2 0 3, call (.p 4) .shr 1 [0], read 0 4, free 0]),
     getFn] := by refine ⟨?_, ?_, ?_⟩ <;> decide +kernel

/-- ... but not written through when the argument is const. -/
theorem no_getter_const : check
    [unitFn [.shr] 3 [call (.p 1) .excl 1 [0], take 2 1 0], getFn] = false := by decide +kernel

/-- Set takes on success, the caller frees on failure. -/
theorem ok_set : Accepted
    [unitFn [] 3 [new 0, new 1, call (.b 0) .shr 1 [0, 1], ite 0 skip (free 1), free 0],
     ⟨[.excl, .succ], .bool, 2, sq [havoc 0, ite 0 (sq [store 0 0 1, ret (.lit true)]) (ret (.lit false))]⟩] := by
  refine ⟨?_, ?_, ?_⟩ <;> decide +kernel

/-- Rule 10 passed on: both ways to forward the result. -/
theorem ok_set_forward : check
    [⟨[.excl, .succ], .bool, 2, sq [call (.b 0) .shr 0 [0, 1], ret (.bvar 0)]⟩,
     ⟨[.excl, .succ], .bool, 2, sq [call (.b 0) .shr 0 [0, 1], ite 0 (ret (.lit true)) (ret (.lit false))]⟩] = true := by
  decide +kernel

def push : Fn := unitFn [.excl, .own] 3 [take 2 0 0, store 1 0 2, store 0 0 1]
def pop : Fn := ⟨[.excl], .own, 3, sq [take 1 0 0, test 0 1,
  ite 0 (sq [take 2 1 0, store 0 0 2, ret (.ptr 1)]) (ret (.ptr 1))]⟩

theorem ok_list : Accepted
    [unitFn [] 5 [new 0, new 1, call .none .shr 1 [0, 1], new 2, call .none .shr 1 [0, 2],
      call (.p 3) .shr 2 [0], call (.p 4) .shr 2 [0], read 0 3, free 3, free 4, call (.p 3) .shr 2 [0], free 3, free 0],
     push, pop] := by refine ⟨?_, ?_, ?_⟩ <;> decide +kernel

/-- `void free_all(T *_Owned n) { if (n) { free_all(_Take(n->next)); free(n); } }` -/
def freeAll (self : Nat) : Fn := ⟨[.own], .unit, 2, sq [test 0 0,
  ite 0 (sq [take 1 0 0, call .none .shr self [1], free 0]) skip, ret .unit]⟩

theorem ok_free_rec : Accepted
    [unitFn [] 3 (mk2 ++ [call .none .shr 1 [0]]), freeAll 1] := by refine ⟨?_, ?_, ?_⟩ <;> decide +kernel

/-- Walk to the last node and append: `c = n; n = c->next;` in a loop. -/
def append : Fn := unitFn [.excl, .own] 4 [borrow 2 .excl 0, step 3 .excl 2 0, test 0 3,
  .while 0 (sq [borrow 2 .excl 3, step 3 .excl 2 0, test 0 3]), store 2 0 1]

theorem ok_append : Accepted
    [unitFn [] 3 (mk2 ++ [new 2, call .none .shr 1 [0, 2], call .none .shr 2 [0]]), append, freeAll 2] := by
  refine ⟨?_, ?_, ?_⟩ <;> decide +kernel

/-- Single cursor `c = c->next` in a loop, then a sibling is still usable. -/
theorem ok_cursor : check [unitFn [.excl] 3 [borrow 1 .excl 0, borrow 2 .shr 1, havoc 0,
    .while 0 (sq [step 1 .excl 1 0, havoc 0]), read 0 2, read 0 1]] = true := by decide +kernel

/-- Free the list in a loop, popping each node. -/
theorem ok_free_loop : check [unitFn [.own] 3 [mv 1 0, test 0 1,
    .while 0 (sq [take 2 1 0, free 1, mv 1 2, test 0 1])]] = true := by decide +kernel

/-- Branches with different parents join at the common ancestor. -/
theorem ok_join : check [unitFn [.excl] 4 [borrow 1 .excl 0, havoc 0,
    ite 0 (step 2 .excl 0 0) (step 2 .excl 1 0), take 3 2 0]] = true := by decide +kernel

end SafeC.Core
