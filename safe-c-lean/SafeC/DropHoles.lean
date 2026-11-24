-- _Drop rules: for each rule, a program that passes the checker with that rule
-- off and reaches use after free, double free or invalid free; then accepted
-- idioms under the full rules. All checked by evaluation in the kernel.
import SafeC.DropSound

namespace SafeC.Drop

open Stmt

def seqs : List Stmt → Stmt
  | [] => .skip
  | [s] => s
  | s :: ss => .seq s (seqs ss)

def ok (R : Rules) (s : Stmt) : Bool := (check R s).isSome

def ubOf (o : Out) : Option UB :=
  match o with
  | .ub k => some k
  | _ => none

/-! Pointer locals: 0 is `p`, 1 is `q`, 2 is `r`, 3 is `keep`. -/

/-! ## Each rule is necessary -/

/-- `{ int *p _Drop_free = malloc(4); int *_Owned q = p; } free(q);`: the
cleanup frees the block `q` now owns (safe-c-drop.c `moved`, `keep(p)`). -/
def cMove : Stmt := seqs [block 0 (move 1 0), free 1]
theorem need_noMoveByName :
    ok { Rules.full with noMoveByName := false } cMove = true ∧
    ubOf (run cMove) = some .doubleFree ∧ ok Rules.full cMove = false := by decide

/-- Hole 18: `{ int *p _Drop_free = malloc(4); keep = p; } *keep;` -/
def c18 : Stmt := seqs [block 0 (copy 3 0), deref 3]
theorem need_copiesDie :
    ok { Rules.full with copiesDie := false } c18 = true ∧
    ubOf (run c18) = some .useAfterFree ∧ ok Rules.full c18 = false := by decide

/-- `{ int *p _Drop_free = malloc(4); free(p); }` (safe-c-drop.c `freed_by_name`). -/
def cFree : Stmt := block 0 (free 0)
theorem need_noManualFree :
    ok { Rules.full with noManualFree := false } cFree = true ∧
    ubOf (run cFree) = some .doubleFree ∧ ok Rules.full cFree = false := by decide

/-- `int a; { int *p _Drop_free = malloc(4); p = &a; }`: the cleanup frees
the address of a local. The compiler accepts this (see DROP.md). -/
def cAddr : Stmt := block 0 (addr 0 7)
theorem need_dropHoldsOwner :
    ok { Rules.full with dropHoldsOwner := false } cAddr = true ∧
    ubOf (run cAddr) = some .invalidFree ∧ ok Rules.full cAddr = false := by decide

/-- `{ int *q _Drop_free = malloc(4); { int *p _Drop_free = malloc(4); p = q; } }`:
both cleanups free `q`'s block. The compiler rejects this one, through its
lifetime analysis. -/
def cBorrowIn : Stmt := block 1 (block 0 (copy 0 1))
theorem need_dropHoldsOwner_borrow :
    ok { Rules.full with dropHoldsOwner := false } cBorrowIn = true ∧
    ubOf (run cBorrowIn) = some .doubleFree ∧ ok Rules.full cBorrowIn = false := by decide

/-! ## Idioms under the full rules -/

/-- h18_take: `{ int *p _Drop_free = malloc(4); keep = _Take(p); } *keep; free(keep);` -/
def cTakeKeep : Stmt := seqs [block 0 (take 3 0), deref 3, free 3]
theorem take_keep_ok : ok Rules.full cTakeKeep = true := by decide

/-- `{ int *p _Drop_free = malloc(4); free(_Take(p)); }`, as in `buf_free`. -/
def cTakeFree : Stmt := block 0 (seqs [take 1 0, free 1])
theorem take_free_ok : ok Rules.full cTakeFree = true := by decide

/-- A borrow used inside the scope: `{ int *p _Drop_free = malloc(4); q = p; *q; *p; }` -/
def cBorrow : Stmt := block 0 (seqs [copy 1 0, deref 1, deref 0])
theorem borrow_in_scope_ok : ok Rules.full cBorrow = true := by decide

/-- Early return: `{ int *p _Drop_free = malloc(4); *p; if (c) return; keep = _Take(p); }
*keep; free(keep);`. The cleanup runs on the return path too. -/
def cEarlyRet (c : Bool) : Stmt :=
  seqs [block 0 (seqs [deref 0, branch c ret skip, take 3 0]), deref 3, free 3]
theorem early_return_ok : ok Rules.full (cEarlyRet true) = true ∧
    ok Rules.full (cEarlyRet false) = true := by decide

/-- `break` out of the block: `do { int *p _Drop_free = malloc(4); q = p;
if (c) break; *q; } while (0);` -/
def cBreak (c : Bool) : Stmt := block 0 (seqs [copy 1 0, branch c brk skip, deref 1])
theorem break_ok : ok Rules.full (cBreak true) = true ∧ ok Rules.full (cBreak false) = true := by
  decide

/-- Hole 18 on the `break` path: the copy dies there too. -/
def cBreak18 : Stmt := seqs [block 0 (seqs [copy 3 0, branch true brk skip]), deref 3]
theorem break_copy_rejected : ok Rules.full cBreak18 = false := by decide

/-- A conditional take: after the block `keep` is null or owns the block. -/
def cCondTake (c : Bool) : Stmt := seqs [block 0 (branch c (take 3 0) skip), free 3]
theorem cond_take_ok : ok Rules.full (cCondTake true) = true ∧
    ok Rules.full (cCondTake false) = true := by decide

/-- Nested blocks: `{ int *p _Drop_free = malloc(4); { int *q _Drop_free = malloc(4);
r = p; *r; keep = _Take(q); } *r; *keep; } free(keep);`. The inner exit ends
only borrows of `q`; `r` borrows `p` and lives on until `p`'s scope ends. -/
def cNested : Stmt :=
  seqs [block 0 (seqs [block 1 (seqs [copy 2 0, deref 2, take 3 1]), deref 2, deref 3]), free 3]
theorem nested_ok : ok Rules.full cNested = true := by decide

/-- After the outer block, `r` is dead with it. -/
def cNestedLate : Stmt := seqs [cNested, deref 2]
theorem nested_late_rejected : ok Rules.full cNestedLate = false := by decide

/-! ## The idioms are safe for every amount of fuel -/

theorem accepted_sound (s : Stmt) (h : ok Rules.full s = true) (fuel : Nat) :
    (exec fuel St.init s).isUB = false := by
  obtain ⟨r, hr⟩ := Option.isSome_iff_exists.mp h
  exact drop_sound s r hr fuel

theorem idioms_sound (fuel : Nat) (c : Bool) :
    (exec fuel St.init cTakeKeep).isUB = false ∧
    (exec fuel St.init cTakeFree).isUB = false ∧
    (exec fuel St.init cBorrow).isUB = false ∧
    (exec fuel St.init (cEarlyRet c)).isUB = false ∧
    (exec fuel St.init (cBreak c)).isUB = false ∧
    (exec fuel St.init (cCondTake c)).isUB = false ∧
    (exec fuel St.init cNested).isUB = false := by
  refine ⟨accepted_sound _ take_keep_ok fuel, accepted_sound _ take_free_ok fuel,
    accepted_sound _ borrow_in_scope_ok fuel, ?_, ?_, ?_, accepted_sound _ nested_ok fuel⟩ <;>
    cases c
  · exact accepted_sound _ early_return_ok.2 fuel
  · exact accepted_sound _ early_return_ok.1 fuel
  · exact accepted_sound _ break_ok.2 fuel
  · exact accepted_sound _ break_ok.1 fuel
  · exact accepted_sound _ cond_take_ok.2 fuel
  · exact accepted_sound _ cond_take_ok.1 fuel

end SafeC.Drop
