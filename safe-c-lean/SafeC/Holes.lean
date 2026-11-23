-- The review's design holes as concrete well-typed programs that reach UB
-- under the rule that has the hole, checked by evaluation in the kernel.
import SafeC.Defs

namespace SafeC

def Out.isTrap : Out → Bool
  | .trap => true
  | _ => false

def run (R : Rules) (s : Stmt) : Out := exec R false 100 (fun _ => .uninit) [] s

/-- Typing derivations for the small programs below are found by search. -/
macro "typecheck" : tactic =>
  `(tactic| (apply Exists.intro; repeat' (first | decide | rfl | (simp_all [Sub, Ty.ptrFree, Ty.isNonnullPtr, Facts.assign, Facts.empty, Facts.meet, narrow]; done) | constructor!)))

open Expr Stmt Kind Null Ty

def seqs : List Stmt → Stmt
  | [] => .skip
  | [s] => s
  | s :: ss => .seq s (seqs ss)

/-! ### Hole 1: zero-filled nonnull pointer slots

`int *slot[1]` zero-filled, read, dereferenced. -/

def h1D : Decls := [(0, ptr nonnull indexable (ptr nonnull single int)), (1, ptr nonnull single int), (2, int)]
def h1P : Stmt := seqs
  [ alloc 0 (ptr nonnull single int) 1,
    assign 1 (index indexable (var 0) (lit 0)),
    assign 2 (index single (var 1) (lit 0)) ]

theorem hole1_typed (R : Rules) (h : R.zeroNonnull = true) : ∃ G, HasTyS R h1D false .empty h1P G := by typecheck

/-- Before the review's fix (null accesses not checked): UB. -/
theorem hole1_ub_preReview : (run Rules.preReview h1P).isUB = true := by decide
/-- With the fix (null accesses trap): a trap. -/
theorem hole1_trap_current : (run Rules.current h1P).isTrap = true := by decide

/-! ### Hole 2: type confusion through a pointee cast (or a union)

2a: `union { int i; int *p; }`: store an int, read it back as a pointer.
2b: `int **pp` laundered through `char *` and back. -/

def h2aD : Decls := [(0, ptr nonnull indexable int), (1, ptr nonnull indexable (ptr nonnull single int)),
  (2, ptr nonnull single int), (3, int)]
def h2aP : Stmt := seqs
  [ alloc 0 int 1,
    store indexable (var 0) (lit 0) (lit 0x41),
    assign 1 (cast (ptr nonnull single int) (var 0)),
    assign 2 (index indexable (var 1) (lit 0)),
    assign 3 (index single (var 2) (lit 0)) ]

theorem hole2a_typed : ∃ G, HasTyS Rules.current h2aD false .empty h2aP G := by typecheck
theorem hole2a_ub : (run Rules.current h2aP).isUB = true := by decide

def h2bD : Decls := [(0, ptr nonnull indexable (ptr nonnull single int)), (1, ptr nonnull indexable char),
  (2, ptr nonnull indexable (ptr nonnull single int)), (3, ptr nonnull single int), (4, int)]
def h2bP : Stmt := seqs
  [ alloc 0 (ptr nonnull single int) 1,
    assign 1 (cast char (var 0)),
    store indexable (var 1) (lit 0) (lit 0x41),
    assign 2 (cast (ptr nonnull single int) (var 1)),
    assign 3 (index indexable (var 2) (lit 0)),
    assign 4 (index single (var 3) (lit 0)) ]

theorem hole2b_typed : ∃ G, HasTyS Rules.current h2bD false .empty h2bP G := by typecheck
theorem hole2b_ub : (run Rules.current h2bP).isUB = true := by decide

/-- 2c: a cast that only changes the nullability of the pointee
(`int *_Nonnull *` to `int *_Nullable *`) lets a null be stored into a slot
the original pointer reads as nonnull. -/
def h2cD : Decls := [(0, ptr nonnull indexable (ptr nonnull single int)), (1, ptr nonnull indexable int),
  (2, ptr nonnull indexable (ptr nullable single int)), (3, ptr nonnull single int), (4, int)]
def h2cP : Stmt := seqs
  [ alloc 1 int 1,
    alloc 0 (ptr nonnull single int) 1,
    store indexable (var 0) (lit 0) (conv indexable single (var 1)),
    assign 2 (cast (ptr nullable single int) (var 0)),
    store indexable (var 2) (lit 0) (null single int),
    assign 3 (index indexable (var 0) (lit 0)),
    assign 4 (index single (var 3) (lit 0)) ]


theorem hole2c_typed : ∃ G, HasTyS Rules.preReview h2cD false .empty h2cP G := by typecheck
theorem hole2c_ub : (run Rules.preReview h2cP).isUB = true := by decide

/-! ### Hole 3: null narrowing survives a call that clears the pointer

`if (g) { clear(); return *g; }` -/

def h3D : Decls := [(0, ptr nullable indexable int), (1, int)]
def h3P : Stmt := seqs
  [ alloc 0 int 1,
    ifNull 0 skip (seqs
      [ call (assign 0 (null indexable int)),
        assign 1 (index indexable (var 0) (lit 0)) ]) ]

theorem hole3_typed :
    (∃ G, HasTyS Rules.preReview h3D false .empty h3P G) ∧ (∃ G, HasTyS Rules.current h3D false .empty h3P G) :=
  ⟨by typecheck, by typecheck⟩

theorem hole3_ub_preReview : (run Rules.preReview h3P).isUB = true := by decide
/-- Under the current rules the null access traps, so hole 3 is no longer UB. -/
theorem hole3_trap_current : (run Rules.current h3P).isTrap = true := by decide

/-! ### Hole 4: checked conversion to null-terminated, then an alias erases the terminator

`char buf[2] = "a"; f(buf);` where `f` walks its `char *__null_terminated`
argument after `buf[1] = 'b'` through the indexable alias. -/

def h4D : Decls := [(0, ptr nonnull indexable char), (1, ptr nonnull nullterm char), (2, char)]
def h4P : Stmt := seqs
  [ alloc 0 char 2,
    store indexable (var 0) (lit 0) (lit 97),
    assign 1 (conv indexable nullterm (var 0)),
    store indexable (var 0) (lit 1) (lit 98),
    assign 1 (padd nullterm (var 1) (lit 1)),
    assign 1 (padd nullterm (var 1) (lit 1)),
    assign 2 (index nullterm (var 1) (lit 0)) ]

theorem hole4_typed (R : Rules) : ∃ G, HasTyS R h4D false .empty h4P G := by typecheck
theorem hole4_ub_current : (run Rules.current h4P).isUB = true := by decide
/-- With bounded null-terminated pointers the read past the old terminator traps. -/
theorem hole4_trap_bounded : (run { Rules.current with ntBounded := true } h4P).isTrap = true := by decide

end SafeC
