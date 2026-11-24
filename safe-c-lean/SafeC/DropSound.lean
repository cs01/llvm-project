-- Soundness of the _Drop checker: a program it accepts under the full rules
-- never uses a freed block, frees a block twice, or frees a non-heap pointer,
-- on any exit path through any number of nested cleanups.
import SafeC.Drop

namespace SafeC.Drop

/-- A value an owner may hold: null or a live heap block. -/
def live (st : St) : Val → Prop
  | .null => True
  | .heap b => st.alive b = true
  | .loc _ => False

/-- `v` owns what it holds: an owner local, or a `_Drop` variable in scope,
whose cleanup will free it whatever the abstract state says. -/
def Owner (D : List Nat) (A : Abs) (v : Nat) : Prop := A v = .own ∨ v ∈ D

/-- The abstract state describes the concrete one. Owners hold null or live
blocks, no two owners hold the same block, and a borrow holds its owner's
value; so freeing an owner's block (by name or by its cleanup) leaves every
other owner and every other borrow live. -/
structure Inv (D : List Nat) (A : Abs) (st : St) : Prop where
  bnd : ∀ v b, st.ρ v = .heap b → b < st.next
  nul : ∀ v, A v = .null → st.ρ v = .null
  own : ∀ v, Owner D A v → live st (st.ρ v)
  uniq : ∀ v w b, v ≠ w → Owner D A v → Owner D A w → st.ρ v = .heap b → st.ρ w ≠ .heap b
  bor : ∀ v z, A v = .borrow z → A z = .own ∧ st.ρ v = st.ρ z
  loc : ∀ v, A v = .loc → ∃ a, st.ρ v = .loc a

def Good (D : List Nat) (r : Res) : Out → Prop
  | .ok st => ∃ A, r.n = some A ∧ Inv D A st
  | .brk st => ∃ A, r.b = some A ∧ Inv D A st
  | .ret st => ∃ A, r.r = some A ∧ Inv D A st
  | .ub _ => False
  | .trap | .outOfFuel => True

/-! ## Updates -/

theorem upd_same {α : Type} (f : Nat → α) (x : Nat) (v : α) : upd f x v x = v := by simp [upd]

theorem upd_other {α : Type} (f : Nat → α) {x y : Nat} (v : α) (h : y ≠ x) :
    upd f x v y = f y := by simp [upd, h]

theorem killB_borrow {y : Nat} {A : Abs} {v z : Nat} (h : killB y A v = .borrow z) :
    A v = .borrow z ∧ z ≠ y := by
  unfold killB at h
  split at h
  · rename_i z' hz; split at h
    · cases h
    · cases h; exact ⟨hz, by assumption⟩
  · rename_i hn; exact absurd h (hn z)

/-- `killB` changes only borrows. -/
theorem killB_of {y : Nat} {A : Abs} {v : Nat} {a : Av} (ha : ∀ z, a ≠ .borrow z) (ha' : a ≠ .dead)
    (h : killB y A v = a) : A v = a := by
  unfold killB at h
  split at h
  · split at h
    · exact absurd h.symm ha'
    · exact absurd h.symm (ha _)
  · exact h

theorem killB_own {y : Nat} {A : Abs} {v : Nat} (h : A v = .own) : killB y A v = .own := by
  simp [killB, h]

theorem owner_killB {D : List Nat} {y : Nat} {A : Abs} {v : Nat} :
    Owner D (killB y A) v ↔ Owner D A v := by
  unfold Owner
  constructor
  · rintro (h | h)
    · exact .inl (killB_of (by simp) (by simp) h)
    · exact .inr h
  · rintro (h | h)
    · exact .inl (killB_own h)
    · exact .inr h

/-- Write `v'` into `y`, with abstract value `a`; borrows of `y` die. -/
theorem inv_assign {D : List Nat} {A : Abs} {st : St} (hI : Inv D A st) (y : Nat) (v' : Val)
    (a : Av)
    (hb : ∀ b, v' = .heap b → b < st.next)
    (hn : a = .null → v' = .null)
    (ho : a = .own ∨ y ∈ D →
      live st v' ∧ ∀ w b, w ≠ y → Owner D A w → v' = .heap b → st.ρ w ≠ .heap b)
    (hz : ∀ z, a = .borrow z → z ≠ y ∧ A z = .own ∧ v' = st.ρ z)
    (hl : a = .loc → ∃ l, v' = .loc l) :
    Inv D (upd (killB y A) y a) (st.setV y v') := by
  have hown : ∀ w, w ≠ y → (Owner D (upd (killB y A) y a) w ↔ Owner D A w) := by
    intro w hw; unfold Owner; rw [upd_other _ _ hw]; exact owner_killB
  have hy : Owner D (upd (killB y A) y a) y ↔ a = .own ∨ y ∈ D := by
    unfold Owner; rw [upd_same]
  refine ⟨fun v b h => ?_, fun v h => ?_, fun v h => ?_, fun v w b hvw hv hw h1 h2 => ?_,
    fun v z h => ?_, fun v h => ?_⟩
  · by_cases hv : v = y
    · subst hv; simp only [St.setV, upd_same] at h; exact hb b h
    · simp only [St.setV, upd_other _ _ hv] at h; exact hI.bnd v b h
  · by_cases hv : v = y
    · subst hv; simp only [upd_same] at h; simpa [St.setV, upd_same] using hn h
    · simp only [upd_other _ _ hv] at h
      simpa [St.setV, upd_other _ _ hv] using hI.nul v (killB_of (by simp) (by simp) h)
  · by_cases hv : v = y
    · subst hv; simpa [St.setV, upd_same, live] using (ho (hy.mp h)).1
    · simpa [St.setV, upd_other _ _ hv, live] using hI.own v ((hown v hv).mp h)
  · by_cases hvy : v = y
    · subst hvy
      have hwy : w ≠ v := fun e => hvw e.symm
      simp only [St.setV, upd_same, upd_other _ _ hwy] at h1 h2 ⊢
      exact (ho (hy.mp hv)).2 w b hwy ((hown w hwy).mp hw) h1 h2
    · by_cases hwy : w = y
      · subst hwy
        simp only [St.setV, upd_same, upd_other _ _ hvy] at h1 h2 ⊢
        exact (ho (hy.mp hw)).2 v b hvy ((hown v hvy).mp hv) h2 h1
      · simp only [St.setV, upd_other _ _ hvy, upd_other _ _ hwy] at h1 h2 ⊢
        exact hI.uniq v w b hvw ((hown v hvy).mp hv) ((hown w hwy).mp hw) h1 h2
  · by_cases hv : v = y
    · subst hv; simp only [upd_same] at h
      obtain ⟨hzy, hzo, he⟩ := hz z h
      refine ⟨?_, ?_⟩
      · rw [upd_other _ _ hzy]; exact killB_own hzo
      · simp [St.setV, upd_same, upd_other _ _ hzy, he]
    · simp only [upd_other _ _ hv] at h
      obtain ⟨h, hzy⟩ := killB_borrow h
      obtain ⟨hzo, he⟩ := hI.bor v z h
      refine ⟨?_, ?_⟩
      · rw [upd_other _ _ hzy]; exact killB_own hzo
      · simp [St.setV, upd_other _ _ hv, upd_other _ _ hzy, he]
  · by_cases hv : v = y
    · subst hv; simp only [upd_same] at h; simpa [St.setV, upd_same] using hl h
    · simp only [upd_other _ _ hv] at h
      simpa [St.setV, upd_other _ _ hv] using hI.loc v (killB_of (by simp) (by simp) h)

/-- Forget `x` (it was freed, moved from, or left scope) and drop
`_Drop` variables from `D`. -/
theorem inv_forget {D D' : List Nat} {A : Abs} {st : St} (hI : Inv D' A st)
    (hD : ∀ v, v ∈ D → v ∈ D') (x : Nat) : Inv D (upd (killB x A) x .dead) st := by
  have hown : ∀ w, Owner D (upd (killB x A) x .dead) w → Owner D' A w := by
    intro w h
    rcases h with h | h
    · by_cases hw : w = x
      · subst hw; simp [upd_same] at h
      · rw [upd_other _ _ hw] at h; exact .inl (killB_of (by simp) (by simp) h)
    · exact .inr (hD w h)
  refine ⟨hI.bnd, fun v h => ?_, fun v h => hI.own v (hown v h),
    fun v w b hvw hv hw => hI.uniq v w b hvw (hown v hv) (hown w hw), fun v z h => ?_, fun v h => ?_⟩
  · by_cases hv : v = x
    · subst hv; simp [upd_same] at h
    · rw [upd_other _ _ hv] at h; exact hI.nul v (killB_of (by simp) (by simp) h)
  · by_cases hv : v = x
    · subst hv; simp [upd_same] at h
    · rw [upd_other _ _ hv] at h
      obtain ⟨h, hzx⟩ := killB_borrow h
      obtain ⟨hzo, he⟩ := hI.bor v z h
      exact ⟨by rw [upd_other _ _ hzx]; exact killB_own hzo, he⟩
  · by_cases hv : v = x
    · subst hv; simp [upd_same] at h
    · rw [upd_other _ _ hv] at h; exact hI.loc v (killB_of (by simp) (by simp) h)

/-- Free block `b` when no owner holds it. -/
theorem inv_free {D : List Nat} {A : Abs} {st : St} (hI : Inv D A st) (b : Nat)
    (hb : ∀ v, Owner D A v → st.ρ v ≠ .heap b) :
    Inv D A { st with alive := upd st.alive b false } := by
  refine ⟨hI.bnd, hI.nul, fun v h => ?_, hI.uniq, hI.bor, hI.loc⟩
  have hl := hI.own v h
  have hne := hb v h
  revert hl hne
  cases st.ρ v with
  | null => intros; trivial
  | loc _ => intro hl; exact absurd hl id
  | heap c =>
    intro hl hne
    have hcb : c ≠ b := fun e => hne (by rw [e])
    simpa [live, upd_other _ _ hcb] using hl

theorem inv_grow {D : List Nat} {A : Abs} {st : St} (hI : Inv D A st) : Inv D A st.grow := by
  refine ⟨fun v b h => Nat.lt_succ_of_lt (hI.bnd v b h), hI.nul, fun v h => ?_, hI.uniq, hI.bor,
    hI.loc⟩
  have hl := hI.own v h
  show live st.grow (st.ρ v)
  revert hl
  cases st.ρ v with
  | null => intro; trivial
  | loc _ => intro hl; exact hl
  | heap c =>
    intro hl
    simp only [live, St.grow, upd] at hl ⊢
    split <;> simp_all

theorem inv_push {D : List Nat} {A : Abs} {st : St} (hI : Inv D A st) {x : Nat}
    (hx : A x = .own) : Inv (x :: D) A st := by
  have hown : ∀ w, Owner (x :: D) A w → Owner D A w := by
    rintro w (h | h)
    · exact .inl h
    · rcases List.mem_cons.mp h with h | h
      · subst h; exact .inl hx
      · exact .inr h
  exact ⟨hI.bnd, hI.nul, fun v h => hI.own v (hown v h),
    fun v w b hvw hv hw => hI.uniq v w b hvw (hown v hv) (hown w hw), hI.bor, hI.loc⟩

/-- Entering block `x`: `x` owns a fresh block. -/
theorem inv_alloc {D : List Nat} {A : Abs} {st : St} (hI : Inv D A st) (x : Nat) :
    Inv (x :: D) (upd (killB x A) x .own) (alloc st x) := by
  have hg := inv_grow hI
  refine inv_push (inv_assign hg x (.heap st.next) .own (fun b h => ?_) (by simp) (fun _ => ?_)
    (by simp) (by simp)) (upd_same _ _ _)
  · cases h; exact Nat.lt_succ_self _
  · refine ⟨by simp [live, St.grow, upd], fun w b _ _ h h' => ?_⟩
    cases h
    exact Nat.lt_irrefl _ (hI.bnd w _ h')

/-- Leaving block `x`: the cleanup frees what `x` holds, which no other owner
holds, so it never fails. -/
theorem inv_leave {D : List Nat} {A : Abs} {st : St} {x : Nat} (hI : Inv (x :: D) A st)
    (hx : x ∉ D) :
    ∃ st', freeV st (st.ρ x) = .ok st' ∧ Inv D (upd (killB x A) x .dead) st' := by
  have hF := inv_forget hI (fun v h => List.mem_cons_of_mem x h) x
  have hl := hI.own x (.inr (List.mem_cons_self ..))
  have hb : ∀ b, st.ρ x = .heap b → ∀ v, Owner D (upd (killB x A) x .dead) v → st.ρ v ≠ .heap b := by
    intro b hb v hv
    by_cases hvx : v = x
    · subst hvx
      rcases hv with h | h
      · simp [upd_same] at h
      · exact absurd h hx
    · have hv' : Owner (x :: D) A v := by
        rcases hv with h | h
        · rw [upd_other _ _ hvx] at h; exact .inl (killB_of (by simp) (by simp) h)
        · exact .inr (List.mem_cons_of_mem x h)
      exact hI.uniq x v b (Ne.symm hvx) (.inr (List.mem_cons_self ..)) hv' hb
  revert hl hb
  cases st.ρ x with
  | null => intros; exact ⟨st, rfl, hF⟩
  | loc _ => intro hl; exact absurd hl id
  | heap b =>
    intro hl hb
    simp only [live] at hl
    exact ⟨_, by simp [freeV, hl], inv_free hF b (hb b rfl)⟩

/-! ## Joins -/

theorem Av.join_null {a b : Av} (h : a.join b = .null) : a = .null ∧ b = .null := by
  cases a <;> cases b <;> simp [Av.join] at h ⊢ <;> split at h <;> cases h

theorem Av.join_own {a b : Av} (h : a.join b = .own) :
    (a = .own ∨ a = .null) ∧ (b = .own ∨ b = .null) := by
  cases a <;> cases b <;> simp [Av.join] at h ⊢ <;> split at h <;> cases h

theorem Av.join_borrow {a b : Av} {z : Nat} (h : a.join b = .borrow z) :
    a = .borrow z ∧ b = .borrow z := by
  cases a <;> cases b <;> simp [Av.join] at h ⊢
  split at h
  · cases h; subst_vars; exact ⟨rfl, rfl⟩
  · cases h

theorem Av.join_loc {a b : Av} (h : a.join b = .loc) : a = .loc ∧ b = .loc := by
  cases a <;> cases b <;> simp [Av.join] at h ⊢ <;> split at h <;> cases h

theorem norm_of {P : Abs} {v : Nat} {a : Av} (ha : ∀ z, a ≠ .borrow z) (ha' : a ≠ .dead)
    (h : norm P v = a) : P v = a := by
  unfold norm at h
  split at h
  · split at h
    · exact absurd h.symm (ha _)
    · exact absurd h.symm ha'
  · exact h

theorem norm_borrow {P : Abs} {v z : Nat} (h : norm P v = .borrow z) :
    P v = .borrow z ∧ P z = .own := by
  unfold norm at h
  split at h
  · rename_i z' hz
    split at h
    · cases h; exact ⟨hz, by assumption⟩
    · cases h
  · rename_i hn; exact absurd h (hn z)

theorem norm_own {P : Abs} {v : Nat} (h : P v = .own) : norm P v = .own := by simp [norm, h]

/-- An invariant for `A` holds for a normalized pointwise join `P` whose every
entry says no more than `A`'s; `A` may be either side of the join. -/
theorem inv_join_of {D : List Nat} {A : Abs} {st : St} (hI : Inv D A st) (P : Abs)
    (hn : ∀ v, P v = .null → A v = .null)
    (ho : ∀ v, P v = .own → A v = .own ∨ A v = .null)
    (hz : ∀ v z, P v = .borrow z → A v = .borrow z)
    (hl : ∀ v, P v = .loc → A v = .loc) : Inv D (norm P) st := by
  have hown : ∀ v, Owner D (norm P) v → st.ρ v = .null ∨ Owner D A v := by
    rintro v (h | h)
    · rcases ho v (norm_of (by simp) (by simp) h) with h | h
      · exact .inr (.inl h)
      · exact .inl (hI.nul v h)
    · exact .inr (.inr h)
  refine ⟨hI.bnd, fun v h => hI.nul v (hn v (norm_of (by simp) (by simp) h)), fun v h => ?_,
    fun v w b hvw hv hw h1 h2 => ?_, fun v z h => ?_, fun v h => hI.loc v (hl v (norm_of (by simp) (by simp) h))⟩
  · rcases hown v h with h | h
    · rw [h]; trivial
    · exact hI.own v h
  · rcases hown v hv with h | hv
    · rw [h] at h1; cases h1
    rcases hown w hw with h | hw
    · rw [h] at h2; cases h2
    exact hI.uniq v w b hvw hv hw h1 h2
  · obtain ⟨h1, h2⟩ := norm_borrow h
    exact ⟨norm_own h2, (hI.bor v z (hz v z h1)).2⟩

theorem inv_join_left {D : List Nat} {A B : Abs} {st : St} (hI : Inv D A st) :
    Inv D (join A B) st :=
  inv_join_of hI _ (fun _ h => (Av.join_null h).1) (fun _ h => (Av.join_own h).1)
    (fun _ _ h => (Av.join_borrow h).1) (fun _ h => (Av.join_loc h).1)

theorem inv_join_right {D : List Nat} {A B : Abs} {st : St} (hI : Inv D B st) :
    Inv D (join A B) st :=
  inv_join_of hI _ (fun _ h => (Av.join_null h).2) (fun _ h => (Av.join_own h).2)
    (fun _ _ h => (Av.join_borrow h).2) (fun _ h => (Av.join_loc h).2)

theorem ojoin_left {D : List Nat} {st : St} {o1 : Option Abs} (o2 : Option Abs)
    (h : ∃ A, o1 = some A ∧ Inv D A st) : ∃ C, ojoin o1 o2 = some C ∧ Inv D C st := by
  obtain ⟨A, rfl, hI⟩ := h
  cases o2 with
  | none => exact ⟨A, rfl, hI⟩
  | some B => exact ⟨_, rfl, inv_join_left hI⟩

theorem ojoin_right {D : List Nat} {st : St} (o1 : Option Abs) {o2 : Option Abs}
    (h : ∃ A, o2 = some A ∧ Inv D A st) : ∃ C, ojoin o1 o2 = some C ∧ Inv D C st := by
  obtain ⟨B, rfl, hI⟩ := h
  cases o1 with
  | none => exact ⟨B, rfl, hI⟩
  | some A => exact ⟨_, rfl, inv_join_right hI⟩

theorem good_join_left {D : List Nat} {r1 : Res} (r2 : Res) {o : Out} (h : Good D r1 o) :
    Good D (r1.join r2) o := by
  cases o with
  | ok _ => exact ojoin_left _ h
  | brk _ => exact ojoin_left _ h
  | ret _ => exact ojoin_left _ h
  | ub _ => exact h
  | trap => trivial
  | outOfFuel => trivial

theorem good_join_right {D : List Nat} (r1 : Res) {r2 : Res} {o : Out} (h : Good D r2 o) :
    Good D (r1.join r2) o := by
  cases o with
  | ok _ => exact ojoin_right _ h
  | brk _ => exact ojoin_right _ h
  | ret _ => exact ojoin_right _ h
  | ub _ => exact h
  | trap => trivial
  | outOfFuel => trivial

/-! ## Statements -/

theorem owns_cases {a : Av} (h : a.owns = true) : a = .own ∨ a = .null := by
  cases a <;> simp_all [Av.owns]

theorem good_only {D : List Nat} {A : Abs} {st : St} (h : Inv D A st) : Good D (only A) (.ok st) :=
  ⟨A, rfl, h⟩

/-- An owner's value (`own` or `null`) is live and no other owner holds it. -/
theorem owned_val {D : List Nat} {A : Abs} {st : St} (hI : Inv D A st) {x : Nat}
    (hx : A x = .own ∨ A x = .null) :
    live st (st.ρ x) ∧ ∀ w b, w ≠ x → Owner D A w → st.ρ x = .heap b → st.ρ w ≠ .heap b := by
  rcases hx with hx | hx
  · exact ⟨hI.own x (.inl hx), fun w b hw hwo h => hI.uniq x w b (Ne.symm hw) (.inl hx) hwo h⟩
  · rw [hI.nul x hx]; exact ⟨trivial, fun _ _ _ _ h => by cases h⟩

/-- What `y = x` stores into a plain pointer `y` that is not a `_Drop`
variable. -/
theorem copy_val {D : List Nat} {A : Abs} {st : St} (hI : Inv D A st) {x y : Nat} {a : Av}
    (hxy : x ≠ y) (hyD : y ∉ D) (ha : (killB y A x).val x = some a) :
    (a = .null → st.ρ x = .null) ∧ (a = .own ∨ y ∈ D → False) ∧
    (∀ z, a = .borrow z → z ≠ y ∧ A z = .own ∧ st.ρ x = st.ρ z) ∧
    (a = .loc → ∃ l, st.ρ x = .loc l) := by
  cases hA : A x with
  | null =>
    simp only [killB, hA, Av.val, Option.some.injEq] at ha; subst ha
    exact ⟨fun _ => hI.nul x hA, by simp [hyD], by simp, by simp⟩
  | own =>
    simp only [killB, hA, Av.val, Option.some.injEq] at ha; subst ha
    exact ⟨by simp, by simp [hyD], fun z h => by cases h; exact ⟨hxy, hA, rfl⟩, by simp⟩
  | borrow z =>
    by_cases hz : z = y
    · simp [killB, hA, hz, Av.val] at ha
    · simp only [killB, hA, hz, ite_false, Av.val, Option.some.injEq] at ha; subst ha
      obtain ⟨h1, h2⟩ := hI.bor x z hA
      exact ⟨by simp, by simp [hyD], fun z' h => by cases h; exact ⟨hz, h1, h2⟩, by simp⟩
  | loc =>
    simp only [killB, hA, Av.val, Option.some.injEq] at ha; subst ha
    exact ⟨by simp, by simp [hyD], by simp, fun _ => hI.loc x hA⟩
  | dead => simp [killB, hA, Av.val] at ha

theorem chk_good :
    ∀ (fuel : Nat) (s : Stmt) (D : List Nat) (A : Abs) (r : Res) (st : St),
      chk Rules.full D A s = some r → Inv D A st → Good D r (exec fuel st s) := by
  intro fuel
  induction fuel with
  | zero => intros; trivial
  | succ n ih =>
    intro s D A r st hc hI
    cases s with
    | skip =>
      simp only [chk, Option.some.injEq] at hc; subst hc; exact good_only hI
    | seq a b =>
      simp only [chk] at hc
      obtain ⟨r1, h1, hc⟩ := Option.bind_eq_some_iff.mp hc
      have ha := ih a D A r1 st h1 hI
      simp only [exec]
      cases hn : r1.n with
      | none =>
        rw [hn] at hc; simp only [Option.some.injEq] at hc; subst hc
        cases ho : exec n st a <;> rw [ho] at ha <;> simp only [Out.then]
        · obtain ⟨_, h, _⟩ := ha; rw [hn] at h; cases h
        all_goals exact ha
      | some A1 =>
        rw [hn] at hc
        obtain ⟨r2, h2, hc⟩ := Option.bind_eq_some_iff.mp hc
        simp only [Option.some.injEq] at hc; subst hc
        cases ho : exec n st a <;> rw [ho] at ha <;> simp only [Out.then]
        · obtain ⟨A', h, hI'⟩ := ha; rw [hn] at h; cases h
          have hb := ih b D A1 r2 _ h2 hI'
          cases ob : exec n _ b <;> rw [ob] at hb
          · exact hb
          · exact ojoin_right _ hb
          · exact ojoin_right _ hb
          all_goals exact hb
        · exact ojoin_left _ ha
        · exact ojoin_left _ ha
        all_goals exact ha
    | branch c a b =>
      simp only [chk] at hc
      obtain ⟨r1, h1, hc⟩ := Option.bind_eq_some_iff.mp hc
      obtain ⟨r2, h2, hc⟩ := Option.bind_eq_some_iff.mp hc
      simp only [Option.some.injEq] at hc; subst hc
      simp only [exec]
      split
      · exact good_join_left _ (ih a D A r1 st h1 hI)
      · exact good_join_right _ (ih b D A r2 st h2 hI)
    | block x body =>
      simp only [chk] at hc
      split at hc
      · cases hc
      rename_i hxD
      have hx : x ∉ D := fun h => hxD (List.contains_iff_mem.mpr h)
      obtain ⟨rb, h1, hc⟩ := Option.bind_eq_some_iff.mp hc
      simp only [Rules.full, ite_true, Option.some.injEq] at hc; subst hc
      have hb := ih body (x :: D) _ rb _ h1 (inv_alloc hI x)
      simp only [exec]
      cases ob : exec n (alloc st x) body <;> rw [ob] at hb <;> simp only [leave]
      · obtain ⟨C, hC, hIC⟩ := ojoin_left rb.b hb
        obtain ⟨st', he, hI'⟩ := inv_leave hIC hx
        rw [he]; exact ⟨_, by rw [hC]; rfl, hI'⟩
      · obtain ⟨C, hC, hIC⟩ := ojoin_right rb.n hb
        obtain ⟨st', he, hI'⟩ := inv_leave hIC hx
        rw [he]; exact ⟨_, by rw [hC]; rfl, hI'⟩
      · obtain ⟨C, hC, hIC⟩ := hb
        obtain ⟨st', he, hI'⟩ := inv_leave hIC hx
        rw [he]; exact ⟨_, by simp only [hC]; rfl, hI'⟩
      all_goals first | trivial | exact hb
    | copy y x =>
      simp only [chk, Rules.full, Bool.true_and] at hc
      split at hc
      · cases hc
      rename_i hg
      simp only [Bool.or_eq_true, decide_eq_true_eq, not_or] at hg
      obtain ⟨hyx, hyD⟩ := hg
      have hyD : y ∉ D := fun h => hyD (List.contains_iff_mem.mpr h)
      obtain ⟨a, ha, hc⟩ := Option.map_eq_some_iff.mp hc
      subst hc
      obtain ⟨h1, h2, h3, h4⟩ := copy_val hI (Ne.symm hyx) hyD ha
      exact good_only (inv_assign hI y (st.ρ x) a (hI.bnd x) h1 (fun h => (h2 h).elim) h3 h4)
    | addr y l =>
      simp only [chk, Rules.full, Bool.true_and] at hc
      split at hc
      · cases hc
      rename_i hyD
      have hyD : y ∉ D := fun h => hyD (List.contains_iff_mem.mpr h)
      simp only [Option.some.injEq] at hc; subst hc
      refine good_only (inv_assign hI y (.loc l) .loc (fun _ h => by cases h) (fun h => by cases h)
        (fun h => ?_) (fun _ h => by cases h) (fun _ => ⟨l, rfl⟩))
      rcases h with h | h
      · cases h
      · exact absurd h hyD
    | take y x =>
      simp only [chk] at hc
      split at hc
      · cases hc
      rename_i hg
      simp only [Bool.or_eq_true, decide_eq_true_eq, Bool.not_eq_true', not_or,
        Bool.not_eq_false] at hg
      obtain ⟨hyx, hxo⟩ := hg
      have hxo := owns_cases hxo
      simp only [Option.some.injEq] at hc; subst hc
      have h1 := inv_assign hI x .null .null (fun _ h => by cases h) (fun _ => rfl)
        (fun _ => ⟨trivial, fun _ _ _ _ h => by cases h⟩) (fun _ h => by cases h) (fun h => by cases h)
      have hov := owned_val hI hxo
      refine good_only (inv_assign h1 y (st.ρ x) (A x) (hI.bnd x) (fun h => hI.nul x h)
        (fun _ => ⟨?_, fun w b hwy hw h => ?_⟩) (fun z h => by rcases hxo with e | e <;> simp_all)
        (fun h => by rcases hxo with e | e <;> simp_all))
      · simpa [live, St.setV] using hov.1
      · by_cases hwx : w = x
        · subst hwx; simp [St.setV, upd_same]
        · simp only [St.setV, upd_other _ _ hwx]
          refine hov.2 w b hwx ?_ h
          rcases hw with hw | hw
          · rw [upd_other _ _ hwx] at hw; exact .inl (killB_of (by simp) (by simp) hw)
          · exact .inr hw
    | move y x =>
      simp only [chk, Rules.full, Bool.true_and] at hc
      split at hc
      · cases hc
      rename_i hg
      simp only [Bool.or_eq_true, decide_eq_true_eq, Bool.not_eq_true', not_or,
        Bool.not_eq_false] at hg
      obtain ⟨⟨hyx, hxo⟩, hxD⟩ := hg
      have hxD : x ∉ D := fun h => hxD (List.contains_iff_mem.mpr h)
      have hxo := owns_cases hxo
      simp only [Option.some.injEq] at hc; subst hc
      have h1 := inv_forget hI (fun _ h => h) x
      have hov := owned_val hI hxo
      refine good_only (inv_assign h1 y (st.ρ x) (A x) (hI.bnd x) (fun h => hI.nul x h)
        (fun _ => ⟨hov.1, fun w b hwy hw h => ?_⟩) (fun z h => by rcases hxo with e | e <;> simp_all)
        (fun h => by rcases hxo with e | e <;> simp_all))
      by_cases hwx : w = x
      · subst hwx
        rcases hw with hw | hw
        · simp [upd_same] at hw
        · exact absurd hw hxD
      · refine hov.2 w b hwx ?_ h
        rcases hw with hw | hw
        · rw [upd_other _ _ hwx] at hw; exact .inl (killB_of (by simp) (by simp) hw)
        · exact .inr hw
    | free x =>
      simp only [chk, Rules.full, Bool.true_and] at hc
      split at hc
      · cases hc
      rename_i hg
      simp only [Bool.or_eq_true, Bool.not_eq_true', not_or, Bool.not_eq_false] at hg
      obtain ⟨hxo, hxD⟩ := hg
      have hxD : x ∉ D := fun h => hxD (List.contains_iff_mem.mpr h)
      have hxo := owns_cases hxo
      simp only [Option.some.injEq] at hc; subst hc
      have h1 := inv_forget hI (fun _ h => h) x
      have hov := owned_val hI hxo
      simp only [exec]
      have hl := hov.1
      have hu := hov.2
      revert hl hu
      cases st.ρ x with
      | null => intros; exact good_only h1
      | loc _ => intro hl; exact absurd hl id
      | heap b =>
        intro hl hu
        simp only [live] at hl
        simp only [freeV, hl, ite_true]
        refine good_only (inv_free h1 b fun v hv => ?_)
        by_cases hvx : v = x
        · subst hvx
          rcases hv with hv | hv
          · simp [upd_same] at hv
          · exact absurd hv hxD
        · refine hu v b hvx ?_ rfl
          rcases hv with hv | hv
          · rw [upd_other _ _ hvx] at hv; exact .inl (killB_of (by simp) (by simp) hv)
          · exact .inr hv
    | deref x =>
      simp only [chk] at hc
      split at hc
      · cases hc
      rename_i hd
      simp only [Option.some.injEq] at hc; subst hc
      simp only [exec]
      have hl : live st (st.ρ x) ∨ ∃ l, st.ρ x = .loc l := by
        cases hA : A x <;> simp only [hA, Av.isDead] at hd
        · exact .inl (by rw [hI.nul x hA]; trivial)
        · exact .inl (hI.own x (.inl hA))
        · rename_i z
          obtain ⟨hz, he⟩ := hI.bor x z hA
          exact .inl (by rw [he]; exact hI.own z (.inl hz))
        · exact .inr (hI.loc x hA)
        · exact absurd trivial hd
      split
      · trivial
      · exact good_only hI
      · rename_i b hb
        rw [hb] at hl
        rcases hl with hl | ⟨_, hl⟩
        · simp only [live] at hl; simp only [hl, ite_true]; exact good_only hI
        · cases hl
    | brk =>
      simp only [chk, Option.some.injEq] at hc; subst hc; exact ⟨A, rfl, hI⟩
    | ret =>
      simp only [chk, Option.some.injEq] at hc; subst hc; exact ⟨A, rfl, hI⟩

theorem inv_init : Inv [] Abs.init St.init := by
  refine ⟨fun _ _ h => ?_, fun _ _ => rfl, fun _ _ => trivial, fun _ _ _ _ _ _ h => ?_,
    fun _ _ h => ?_, fun _ h => ?_⟩
  all_goals cases h

/-- If the checker accepts `s` under the full rules, then running it from the
initial state (every pointer null, nothing allocated) never reaches UB, for
any fuel: no use after free, no double free, no free of a non-heap pointer,
on any exit path (fallthrough, `break`, early `return`) through any nesting
of `_Drop` blocks, each of whose cleanups frees its variable on the way out.
What is left is `ok`, `brk`/`ret` at the top, `trap` (a null dereference) or
`outOfFuel`. -/
theorem drop_sound (s : Stmt) (r : Res) (hc : check Rules.full s = some r) (fuel : Nat) :
    (exec fuel St.init s).isUB = false := by
  have := chk_good fuel s [] Abs.init r St.init hc inv_init
  cases ho : exec fuel St.init s <;> rw [ho] at this <;> first | rfl | exact absurd this id

end SafeC.Drop

-- Lists only propext, Classical.choice and Quot.sound.
#print axioms SafeC.Drop.drop_sound
