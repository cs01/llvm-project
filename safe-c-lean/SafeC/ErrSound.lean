-- Soundness of the _Err_or checker: a program it accepts under the full rules
-- never dereferences an error pointer or null (directly or through a
-- parameter) and never takes PTR_ERR of a non-error.
import SafeC.Err

namespace SafeC.Err

/-- `v` is one of the kinds `k` allows. -/
def fits (k : K) : Val → Prop
  | .null => k.n = true
  | .err _ => k.e = true
  | .ptr _ => k.p = true

/-- The abstract state describes the concrete one: each local's value is of
an allowed kind, and an alias record holds only while both hold one value. -/
structure Inv (A : Abs) (st : St) : Prop where
  fit : ∀ v, fits (A.k v) (st.ρ v)
  ali : ∀ v t, A.al v = some t → st.ρ v = st.ρ t

def Good (r : Res) : Out → Prop
  | .ok st => ∃ A, r = some A ∧ Inv A st
  | .ub _ => False
  | .ret _ | .trap | .outOfFuel => True

theorem upd_same {α : Type} (f : Nat → α) (x : Nat) (v : α) : upd f x v x = v := by simp [upd]

theorem upd_other {α : Type} (f : Nat → α) {x y : Nat} (v : α) (h : y ≠ x) :
    upd f x v y = f y := by simp [upd, h]

/-! ## Joins -/

theorem fits_join_l {a b : K} {v : Val} (h : fits a v) : fits (a.join Rules.full b) v := by
  cases v <;> simp_all [fits, K.join, Rules.full]

theorem fits_join_r {a b : K} {v : Val} (h : fits b v) : fits (a.join Rules.full b) v := by
  cases v <;> simp_all [fits, K.join, Rules.full]

theorem inv_join_l {A B : Abs} {st : St} (h : Inv A st) : Inv (A.join Rules.full B) st := by
  refine ⟨fun v => fits_join_l (h.fit v), fun v t ht => ?_⟩
  simp only [Abs.join] at ht
  split at ht
  · exact h.ali v t ht
  · cases ht

theorem inv_join_r {A B : Abs} {st : St} (h : Inv B st) : Inv (A.join Rules.full B) st := by
  refine ⟨fun v => fits_join_r (h.fit v), fun v t ht => ?_⟩
  simp only [Abs.join] at ht
  split at ht
  · rename_i he; exact h.ali v t (he ▸ ht)
  · cases ht

theorem good_ojoin_l {r1 r2 : Res} {o : Out} (h : Good r1 o) : Good (ojoin Rules.full r1 r2) o := by
  cases o with
  | ok st =>
    obtain ⟨A, rfl, hI⟩ := h
    cases r2 with
    | none => exact ⟨A, rfl, hI⟩
    | some B => exact ⟨_, rfl, inv_join_l hI⟩
  | ub _ => exact h
  | _ => trivial

theorem good_ojoin_r {r1 r2 : Res} {o : Out} (h : Good r2 o) : Good (ojoin Rules.full r1 r2) o := by
  cases o with
  | ok st =>
    obtain ⟨B, rfl, hI⟩ := h
    cases r1 with
    | none => exact ⟨B, rfl, hI⟩
    | some A => exact ⟨_, rfl, inv_join_r hI⟩
  | ub _ => exact h
  | _ => trivial

/-! ## Narrowing -/

theorem same_val {A : Abs} {st : St} (hI : Inv A st) {x v : Nat} (h : same A x v = true) :
    st.ρ v = st.ρ x := by
  have hr : st.ρ (root A x) = st.ρ x := by
    unfold root
    cases hx : A.al x with
    | none => rfl
    | some t => exact (hI.ali x t hx).symm
  simp only [same, Bool.or_eq_true, beq_iff_eq] at h
  rcases h with (h | h) | h
  · rw [h]
  · rw [h, hr]
  · rw [hI.ali v _ h, hr]

theorem fits_nar (t : Test) {k : K} {v : Val} (h : fits k v) :
    fits (t.nar Rules.full (t.eval v) k) v := by
  cases t <;> cases v <;> simp_all [fits, Test.nar, Test.eval, Rules.full]

theorem inv_narrow {A : Abs} {st : St} (hI : Inv A st) (t : Test) (x : Nat) :
    Inv (narrow A x (t.nar Rules.full (t.eval (st.ρ x)))) st := by
  refine ⟨fun v => ?_, hI.ali⟩
  simp only [narrow]
  split
  · rename_i h
    have := fits_nar t (hI.fit v)
    rw [same_val hI h] at this ⊢; exact this
  · exact hI.fit v

/-! ## Writes -/

theorem inv_write {A : Abs} {st : St} (hI : Inv A st) (y : Nat) (v' : Val) (k : K)
    (hk : fits k v') (src : Option Nat)
    (hs : ∀ t, src = some t → v' = (st.setV y v').ρ t) :
    Inv (write Rules.full A y k src) (st.setV y v') := by
  refine ⟨fun v => ?_, fun v t ht => ?_⟩
  · by_cases hv : v = y
    · subst hv; simpa [write, Rules.full, St.setV, upd_same] using hk
    · simpa [write, Rules.full, St.setV, upd_other _ _ hv, kill] using hI.fit v
  · by_cases hv : v = y
    · subst hv
      simp only [write, Rules.full, upd_same] at ht
      have := hs t ht
      simp only [St.setV, upd_same] at this ⊢
      exact this
    · simp only [write, Rules.full, ite_true, upd_other _ _ hv, kill] at ht
      split at ht
      · rename_i t' ha
        split at ht
        · cases ht
        · rename_i hty
          cases ht
          simp [St.setV, upd_other _ _ hv, upd_other _ _ hty, hI.ali v _ ha]
      · cases ht

/-! ## The checker's invariant -/

theorem good_ok {A : Abs} {st : St} (h : Inv A st) : Good (some A) (.ok st) := ⟨A, rfl, h⟩

/-- A dereference the checker allows finds a valid pointer. -/
theorem deref_ok {A : Abs} {st : St} (hI : Inv A st) {x : Nat}
    (hn : (A.k x).n = false) (he : (A.k x).e = false) : derefV st (st.ρ x) = .ok st := by
  have := hI.fit x
  revert this
  cases st.ρ x <;> simp_all [fits, derefV]

theorem chk_good : ∀ (n : Nat) (s : Stmt) (A : Abs) (r : Res) (st : St),
    chk Rules.full A s = some r → Inv A st → Good r (exec n st s) := by
  intro n
  induction n with
  | zero => intro s A r st _ _; trivial
  | succ n ih =>
    intro s A r st hc hI
    cases s with
    | skip =>
      simp only [chk, Option.some.injEq] at hc; subst hc; exact good_ok hI
    | seq a b =>
      simp only [chk, Option.bind_eq_some_iff] at hc
      obtain ⟨r1, h1, h2⟩ := hc
      have g1 := ih a A r1 st h1 hI
      simp only [exec]
      cases ho : exec n st a with
      | ok st' =>
        rw [ho] at g1
        obtain ⟨A1, rfl, hI1⟩ := g1
        exact ih b A1 r st' h2 hI1
      | ret st' => trivial
      | trap => trivial
      | ub k => rw [ho] at g1; exact absurd g1 id
      | outOfFuel => trivial
    | branch c a b =>
      simp only [chk, Option.bind_eq_some_iff, Option.some.injEq] at hc
      obtain ⟨r1, h1, r2, h2, rfl⟩ := hc
      simp only [exec]
      split
      · exact good_ojoin_l (ih a A r1 st h1 hI)
      · exact good_ojoin_r (ih b A r2 st h2 hI)
    | test t x a b =>
      simp only [chk, Option.bind_eq_some_iff, Option.some.injEq] at hc
      obtain ⟨r1, h1, r2, h2, rfl⟩ := hc
      simp only [exec]
      split
      · rename_i hb
        have hI' := inv_narrow hI t x
        rw [hb] at hI'
        exact good_ojoin_l (ih a _ r1 st h1 hI')
      · rename_i hb
        have hI' := inv_narrow hI t x
        rw [Bool.not_eq_true] at hb
        rw [hb] at hI'
        exact good_ojoin_r (ih b _ r2 st h2 hI')
    | call x nl v =>
      simp only [chk, Option.some.injEq] at hc; subst hc
      simp only [exec]
      split
      · trivial
      · rename_i hv
        refine good_ok (inv_write hI x v _ ?_ none (by simp))
        cases v <;> simp_all [fits]
    | mkErr x e =>
      simp only [chk, Option.some.injEq] at hc; subst hc
      exact good_ok (inv_write hI x (.err e) ⟨false, true, false⟩ rfl none (by simp))
    | addr x c =>
      simp only [chk, Option.some.injEq] at hc; subst hc
      exact good_ok (inv_write hI x (.ptr c) ⟨false, false, true⟩ rfl none (by simp))
    | copy y x =>
      simp only [chk] at hc
      split at hc
      · cases hc
      · rename_i hyx
        simp only [Option.some.injEq] at hc; subst hc
        refine good_ok (inv_write hI y _ _ (hI.fit x) _ ?_)
        intro t ht
        cases ht
        have hr : same A x (root A x) = true := by simp [same]
        by_cases hty : root A x = y
        · rw [hty]; simp [St.setV, upd_same]
        · simp [St.setV, upd_other _ _ hty, same_val hI hr]
    | deref x =>
      simp only [chk, Rules.full, Bool.true_and] at hc
      split at hc
      · cases hc
      · rename_i h
        simp only [Bool.or_eq_true, not_or, Bool.not_eq_true] at h
        simp only [Option.some.injEq] at hc; subst hc
        simp only [exec, deref_ok hI h.1 h.2]; exact good_ok hI
    | pass x =>
      simp only [chk, Rules.full, Bool.true_and] at hc
      split at hc
      · cases hc
      · rename_i h
        simp only [Bool.or_eq_true, not_or, Bool.not_eq_true] at h
        simp only [Option.some.injEq] at hc; subst hc
        simp only [exec, deref_ok hI h.1 h.2]; exact good_ok hI
    | ptrErr x =>
      simp only [chk, Rules.full, Bool.true_and] at hc
      split at hc
      · cases hc
      · rename_i h
        simp only [Bool.or_eq_true, not_or, Bool.not_eq_true] at h
        simp only [Option.some.injEq] at hc; subst hc
        have hf := hI.fit x
        simp only [exec]
        revert hf
        cases st.ρ x <;> simp_all [fits]
        intro; exact good_ok hI
    | ret =>
      simp only [chk, Option.some.injEq] at hc; subst hc; trivial

theorem inv_init : Inv Abs.init St.init :=
  ⟨fun _ => rfl, fun _ _ h => by cases h⟩

/-- If the checker accepts `s` under the full rules, then running it from the
initial state (every pointer local null) never reaches UB, for any fuel and
any values the callees return: no dereference of an error pointer or of null
(directly or by a callee through a non-`_Err_or` parameter), and no
`PTR_ERR` of a pointer that is not an error. What is left is `ok`, `ret`,
`trap` (a callee broke its non-null contract) or `outOfFuel`. -/
theorem err_sound (s : Stmt) (r : Res) (hc : check Rules.full s = some r) (fuel : Nat) :
    (exec fuel St.init s).isUB = false := by
  have := chk_good fuel s Abs.init r St.init hc inv_init
  cases ho : exec fuel St.init s <;> rw [ho] at this <;> first | rfl | exact absurd this id

end SafeC.Err

-- Lists only propext, Classical.choice and Quot.sound.
#print axioms SafeC.Err.err_sound
