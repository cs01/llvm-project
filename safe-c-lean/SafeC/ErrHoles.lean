-- _Err_or rules: for each rule, a program that passes the checker with that
-- rule off and reaches UB; then accepted idioms under the full rules. All
-- checked by evaluation in the kernel.
import SafeC.ErrSound

namespace SafeC.Err

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

/-! Pointer locals: 0 is `f`, 1 is `g`. `open` is `T *_Err_or filp_open()`,
`openN` is `T *_Nullable _Err_or lookup()`. `ENOMEM` is 12. -/

/-- `f = open()` returning `v`. -/
def open_ (x : Nat) (v : Val) : Stmt := call x false v
/-- `f = openN()` returning `v`. -/
def openN (x : Nat) (v : Val) : Stmt := call x true v

def enomem : Val := .err 12

/-! ## Each rule is necessary -/

/-- `f = filp_open(); f->pos = 0;` -/
def cDeref : Stmt := seqs [open_ 0 enomem, deref 0]
theorem need_derefNeedsCheck :
    ok { Rules.full with derefNeedsCheck := false } cDeref = true ∧
    ubOf (run cDeref) = some .errDeref ∧ ok Rules.full cDeref = false := by decide

/-- `f = filp_open(); if (c) { if (IS_ERR(f)) return; } f->pos = 0;` -/
def cJoin (c : Bool) : Stmt :=
  seqs [open_ 0 enomem, branch c (test .isErr 0 ret skip) skip, deref 0]
theorem need_joinKeepsMaybe :
    ok { Rules.full with joinKeepsMaybe := false } (cJoin false) = true ∧
    ubOf (run (cJoin false)) = some .errDeref ∧ ok Rules.full (cJoin false) = false := by decide

/-- `f = filp_open(); g = f; g->pos = 0;` -/
def cCopy : Stmt := seqs [open_ 0 enomem, copy 1 0, deref 1]
theorem need_copyKeepsKind :
    ok { Rules.full with copyKeepsKind := false } cCopy = true ∧
    ubOf (run cCopy) = some .errDeref ∧ ok Rules.full cCopy = false := by decide

/-- The same rule on re-assignment: `f = filp_open(); if (IS_ERR(f)) return;
f = filp_open(); f->pos = 0;` -/
def cReassign : Stmt :=
  seqs [open_ 0 (.ptr 1), test .isErr 0 ret skip, open_ 0 enomem, deref 0]
theorem need_copyKeepsKind_reassign :
    ok { Rules.full with copyKeepsKind := false } cReassign = true ∧
    ubOf (run cReassign) = some .errDeref ∧ ok Rules.full cReassign = false := by decide

/-- `f = filp_open(); g = f; f = filp_open(); if (IS_ERR(f)) return; g->pos = 0;`:
the test of the new `f` must not narrow `g`, which holds the old one. -/
def cAlias : Stmt :=
  seqs [open_ 0 enomem, copy 1 0, open_ 0 (.ptr 1), test .isErr 0 ret skip, deref 1]
theorem need_aliasDiesOnWrite :
    ok { Rules.full with aliasDiesOnWrite := false } cAlias = true ∧
    ubOf (run cAlias) = some .errDeref ∧ ok Rules.full cAlias = false := by decide

/-- `f = filp_open(); use(f);` for `void use(struct file *p)`. -/
def cParam : Stmt := seqs [open_ 0 enomem, pass 0]
theorem need_paramNeedsCheck :
    ok { Rules.full with paramNeedsCheck := false } cParam = true ∧
    ubOf (run cParam) = some .errDeref ∧ ok Rules.full cParam = false := by decide

/-- `f = lookup(); if (IS_ERR(f)) return; f->pos = 0;` where `lookup` may
return NULL: `IS_ERR(NULL)` is false. -/
def cNull : Stmt := seqs [openN 0 .null, test .isErr 0 ret skip, deref 0]
theorem need_isErrKeepsNull :
    ok { Rules.full with isErrKeepsNull := false } cNull = true ∧
    ubOf (run cNull) = some .nullDeref ∧ ok Rules.full cNull = false := by decide

/-- `f = lookup(); if (IS_ERR_OR_NULL(f)) return PTR_ERR(f);`: on NULL this
returns 0, success, to a caller that then uses no object. -/
def cPtrErr : Stmt := seqs [openN 0 .null, test .isErrOrNull 0 (seqs [ptrErr 0, ret]) skip]
theorem need_ptrErrNeedsErr :
    ok { Rules.full with ptrErrNeedsErr := false } cPtrErr = true ∧
    ubOf (run cPtrErr) = some .notErr ∧ ok Rules.full cPtrErr = false := by decide

/-! ## Idioms under the full rules

Each idiom takes the callee's result `v`; the checker never reads it, so
acceptance holds for every `v` by evaluation. -/

/-- `f = filp_open(); if (IS_ERR(f)) return PTR_ERR(f); f->pos = 0; use(f);` -/
def cEarly (v : Val) : Stmt :=
  seqs [open_ 0 v, test .isErr 0 (seqs [ptrErr 0, ret]) skip, deref 0, pass 0]
theorem early_return_ok (v : Val) : ok Rules.full (cEarly v) = true := rfl

/-- `if (IS_ERR(f)) err = PTR_ERR(f); else f->pos = 0;` -/
def cIfElse (v : Val) : Stmt := seqs [open_ 0 v, test .isErr 0 (ptrErr 0) (deref 0)]
theorem if_else_ok (v : Val) : ok Rules.full (cIfElse v) = true := rfl

/-- `f = lookup(); if (IS_ERR_OR_NULL(f)) return; f->pos = 0;` -/
def cOrNull (v : Val) : Stmt := seqs [openN 0 v, test .isErrOrNull 0 ret skip, deref 0]
theorem or_null_ok (v : Val) : ok Rules.full (cOrNull v) = true := rfl

/-- `f = lookup(); if (IS_ERR(f)) return PTR_ERR(f); if (!f) return; f->pos = 0;` -/
def cErrThenNull (v : Val) : Stmt :=
  seqs [openN 0 v, test .isErr 0 (seqs [ptrErr 0, ret]) skip, test .isNull 0 ret skip, deref 0]
theorem err_then_null_ok (v : Val) : ok Rules.full (cErrThenNull v) = true := rfl

/-- `f = filp_open(); g = f; if (IS_ERR(g)) return; g->pos = 0; f->pos = 0;`:
checking the copy narrows the original too, as the null analysis does. -/
def cCopyCheck (v : Val) : Stmt :=
  seqs [open_ 0 v, copy 1 0, test .isErr 1 ret skip, deref 1, deref 0]
theorem copy_check_ok (v : Val) : ok Rules.full (cCopyCheck v) = true := rfl

/-- `f = ERR_PTR(-ENOMEM); return PTR_ERR(f);` -/
def cMkErr : Stmt := seqs [mkErr 0 12, ptrErr 0, ret]
theorem mk_err_ok : ok Rules.full cMkErr = true := by decide

/-- Testing the wrong pointer: `if (IS_ERR(g)) return; f->pos = 0;` -/
def cWrong : Stmt := seqs [open_ 0 enomem, open_ 1 (.ptr 1), test .isErr 1 ret skip, deref 0]
theorem wrong_pointer_rejected : ok Rules.full cWrong = false := by decide

/-- `IS_ERR` alone does not license a dereference of a nullable `_Err_or`. -/
theorem is_err_nullable_rejected : ok Rules.full (cNull) = false := by decide

/-! ## The idioms are safe for every amount of fuel and every callee result -/

theorem accepted_sound (s : Stmt) (h : ok Rules.full s = true) (fuel : Nat) :
    (exec fuel St.init s).isUB = false := by
  obtain ⟨r, hr⟩ := Option.isSome_iff_exists.mp h
  exact err_sound s r hr fuel

theorem idioms_sound (fuel : Nat) (v : Val) :
    (exec fuel St.init (cEarly v)).isUB = false ∧
    (exec fuel St.init (cIfElse v)).isUB = false ∧
    (exec fuel St.init (cOrNull v)).isUB = false ∧
    (exec fuel St.init (cErrThenNull v)).isUB = false ∧
    (exec fuel St.init (cCopyCheck v)).isUB = false ∧
    (exec fuel St.init cMkErr).isUB = false :=
  ⟨accepted_sound _ (early_return_ok v) fuel, accepted_sound _ (if_else_ok v) fuel,
    accepted_sound _ (or_null_ok v) fuel, accepted_sound _ (err_then_null_ok v) fuel,
    accepted_sound _ (copy_check_ok v) fuel, accepted_sound _ mk_err_ok fuel⟩

end SafeC.Err
