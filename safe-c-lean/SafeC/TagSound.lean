-- Soundness of the tagged-union checker: a program it accepts under the full
-- rules never reads a union member as another member's type.
import SafeC.Tag

namespace SafeC.Tag

/-- The abstract state describes the concrete one: each struct's tag is one
of its possible values; when it counts as written, the stored member is the
one its tag makes live; escaped structs and pointer targets are in `E`. -/
def Inv (L : Lay) (E : List Nat) (A : Abs) (st : St) : Prop :=
  (∀ x, (st.σ x).tag ∈ (A x).1) ∧
  (∀ x, (A x).2 = true → L.live (st.σ x).tag (st.σ x).mem = true) ∧
  (∀ x, st.esc x = true → x ∈ E) ∧
  (∀ q x, st.ptr q = some x → x ∈ E)

def Good (L : Lay) (E : List Nat) (A : Abs) : Out → Prop
  | .ok st => Inv L E A st
  | .ub _ => False
  | _ => True

theorem live_lt {L : Lay} {t m : Nat} (h : L.live t m = true) : t < L.n := by
  unfold Lay.live at h
  unfold Lay.n
  cases hv : L.of[t]? with
  | none => simp [hv] at h
  | some _ => exact (List.getElem?_eq_some_iff.mp hv).1

theorem allLive_mem {L : Lay} {ks : List Nat} {m t : Nat} (h : allLive L ks m = true)
    (ht : t ∈ ks) : L.live t m = true := List.all_eq_true.mp h t ht

/-- Two tags that make `m` and `m'` live are the same tag only if `m = m'`. -/
theorem live_det {L : Lay} {t m m' : Nat} (h : L.live t m = true) (h' : L.live t m' = true) :
    m = m' := by
  unfold Lay.live at h h'
  have h1 := eq_of_beq h
  have h2 := eq_of_beq h'
  rw [h1] at h2
  exact Option.some.inj h2

theorem readM_good {L : Lay} {E : List Nat} {A : Abs} {st : St} {y : Nat} {c : SV} {m : Nat}
    (hI : Inv L E A st) (hm : c.mem = m) : Good L E A (readM L st y c m) := by
  unfold readM
  split
  · rename_i h; exact absurd hm h.1
  · exact ⟨hI.1, hI.2.1, hI.2.2.1, hI.2.2.2⟩

theorem upd_same {α : Type} (f : Nat → α) (x : Nat) (v : α) : upd f x v x = v := by simp [upd]

theorem upd_other {α : Type} (f : Nat → α) {x y : Nat} (v : α) (h : y ≠ x) :
    upd f x v y = f y := by simp [upd, h]

/-- Updating one struct's storage and its abstract entry together. -/
theorem inv_set {L : Lay} {E : List Nat} {A : Abs} {st : St} (hI : Inv L E A st) (x : Nat)
    (c : SV) (a : List Nat × Bool) (h1 : c.tag ∈ a.1) (h2 : a.2 = true → L.live c.tag c.mem = true) :
    Inv L E (upd A x a) (st.setS x c) := by
  refine ⟨fun y => ?_, fun y => ?_, hI.2.2.1, hI.2.2.2⟩
  · by_cases hy : y = x
    · subst hy; simpa [St.setS, upd_same] using h1
    · simpa [St.setS, upd_other _ _ hy] using hI.1 y
  · by_cases hy : y = x
    · subst hy; simpa [St.setS, upd_same] using h2
    · simpa [St.setS, upd_other _ _ hy] using hI.2.1 y

/-- A consistent whole-struct write to an escaped struct, under `kill`. -/
theorem inv_kill_set {L : Lay} {E : List Nat} {A : Abs} {st : St} (hI : Inv L E (kill L E A) st)
    {x : Nat} (hx : x ∈ E) (c : SV) (hc : L.live c.tag c.mem = true) :
    Inv L E (kill L E A) (st.setS x c) := by
  refine ⟨fun y => ?_, fun y _ => ?_, hI.2.2.1, hI.2.2.2⟩
  · by_cases hy : y = x
    · subst hy
      simp only [St.setS, upd_same, kill, List.contains_iff_mem.mpr hx, ite_true]
      exact List.mem_append_right _ (List.mem_range.mpr (live_lt hc))
    · simpa [St.setS, upd_other _ _ hy] using hI.1 y
  · by_cases hy : y = x
    · subst hy; simpa [St.setS, upd_same] using hc
    · simp only [St.setS, upd_other _ _ hy]; exact hI.2.1 y (by assumption)

theorem inv_kill {L : Lay} {E : List Nat} {A : Abs} {st : St} (hI : Inv L E A st) :
    Inv L E (kill L E A) st := by
  refine ⟨fun y => ?_, fun y h => ?_, hI.2.2.1, hI.2.2.2⟩
  · unfold kill; split
    · exact List.mem_append_left _ (hI.1 y)
    · exact hI.1 y
  · unfold kill at h; split at h
    · exact hI.2.1 y h
    · exact hI.2.1 y h

theorem callW_esc (st : St) (ws : List W) : (callW st ws).esc = st.esc := by
  induction ws generalizing st with
  | nil => rfl
  | cons w ws ih => simp only [callW]; rw [ih]; split <;> rfl

theorem inv_callW {L : Lay} {E : List Nat} {A : Abs} :
    ∀ (ws : List W) (st : St), ws.all (fun w => L.live w.V w.m) = true →
      Inv L E (kill L E A) st → Inv L E (kill L E A) (callW st ws)
  | [], _, _, hI => hI
  | w :: ws, st, hw, hI => by
    simp only [List.all_cons, Bool.and_eq_true] at hw
    simp only [callW]
    apply inv_callW ws _ hw.2
    split
    · exact inv_kill_set hI (hI.2.2.1 _ (by assumption)) ⟨w.V, w.m, w.v⟩ hw.1
    · exact hI

theorem inv_join_left {L : Lay} {E : List Nat} {A B : Abs} {st : St} (hI : Inv L E A st) :
    Inv L E (join A B) st :=
  ⟨fun x => List.mem_append_left _ (hI.1 x),
   fun x h => hI.2.1 x (by simp only [join, Bool.and_eq_true] at h; exact h.1),
   hI.2.2.1, hI.2.2.2⟩

theorem inv_join_right {L : Lay} {E : List Nat} {A B : Abs} {st : St} (hI : Inv L E B st) :
    Inv L E (join A B) st :=
  ⟨fun x => List.mem_append_right _ (hI.1 x),
   fun x h => hI.2.1 x (by simp only [join, Bool.and_eq_true] at h; exact h.2),
   hI.2.2.1, hI.2.2.2⟩

/-- Narrowing struct `x` to the tags that pass `p`, on a path where its tag
passes `p`. -/
theorem inv_narrow {L : Lay} {E : List Nat} {A : Abs} {st : St} (hI : Inv L E A st) (x : Nat)
    (p : Nat → Bool) (hp : p (st.σ x).tag = true) :
    Inv L E (upd A x ((A x).1.filter p, (A x).2)) st := by
  have := inv_set hI x (st.σ x) ((A x).1.filter p, (A x).2)
    (List.mem_filter.mpr ⟨hI.1 x, hp⟩) (hI.2.1 x)
  have hs : st.setS x (st.σ x) = st := by
    cases st; simp only [St.setS, St.mk.injEq, and_true]; funext y; by_cases hy : y = x
    · subst hy; simp [upd_same]
    · simp [upd_other _ _ hy]
  rwa [hs] at this

theorem good_then {L : Lay} {E : List Nat} {A A' : Abs} {o : Out} {f : St → Out}
    (ho : Good L E A o) (hf : ∀ st, Inv L E A st → Good L E A' (f st)) : Good L E A' (o.then f) := by
  cases o with
  | ok st => exact hf st ho
  | ub _ => exact absurd ho id
  | trap => trivial
  | outOfFuel => trivial

theorem chk_good (L : Lay) (E : List Nat) :
    ∀ (fuel : Nat) (s : Stmt) (A A' : Abs) (st : St),
      chk Rules.full L E A s = some A' → Inv L E A st → Good L E A' (exec L fuel st s) := by
  intro fuel
  induction fuel with
  | zero => intro s A A' st _ _; trivial
  | succ n ih =>
    intro s A A' st hc hI
    cases s with
    | skip =>
      simp only [chk, Option.some.injEq] at hc; subst hc; exact hI
    | seq a b =>
      simp only [chk] at hc
      obtain ⟨A1, h1, h2⟩ := Option.bind_eq_some_iff.mp hc
      exact good_then (ih a A A1 st h1 hI) fun st' hI' => ih b A1 A' st' h2 hI'
    | settag x V =>
      simp only [chk, Rules.full, ite_true, Option.some.injEq] at hc; subst hc
      exact inv_set hI x _ _ (List.mem_singleton.mpr rfl) (by simp)
    | mset x m v =>
      simp only [chk, Rules.full, Bool.not_true, Bool.false_or] at hc
      split at hc
      · rename_i hl
        simp only [Option.some.injEq] at hc; subst hc
        exact inv_set hI x _ _ (hI.1 x) fun _ => allLive_mem hl (hI.1 x)
      · cases hc
    | mget y x m =>
      simp only [chk, Rules.full, Bool.not_true, Bool.false_or] at hc
      split at hc
      · rename_i hl
        simp only [Option.some.injEq] at hc; subst hc
        simp only [Bool.and_eq_true] at hl
        exact readM_good hI (live_det (hI.2.1 x hl.2) (allLive_mem hl.1 (hI.1 x)))
      · cases hc
    | ifTag x V a b =>
      simp only [chk, Rules.full, ite_true] at hc
      obtain ⟨A1, h1, hc⟩ := Option.bind_eq_some_iff.mp hc
      obtain ⟨A2, h2, hc⟩ := Option.bind_eq_some_iff.mp hc
      simp only [Option.some.injEq] at hc; subst hc
      simp only [exec]
      split
      · rename_i ht
        have := ih a _ A1 st h1 (inv_narrow hI x (· == V) (by simp [ht]))
        cases ho : exec L n st a <;> rw [ho] at this
        · exact inv_join_left this
        all_goals first | trivial | exact this
      · rename_i ht
        have := ih b _ A2 st h2 (inv_narrow hI x (· != V) (by simp [ht]))
        cases ho : exec L n st b <;> rw [ho] at this
        · exact inv_join_right this
        all_goals first | trivial | exact this
    | init x V m v =>
      simp only [chk, Rules.full, Bool.not_true, Bool.false_or] at hc
      split at hc
      · rename_i hl
        simp only [Option.some.injEq] at hc; subst hc
        exact inv_set hI x ⟨V, m, v⟩ _ (List.mem_singleton.mpr rfl) fun _ => hl
      · cases hc
    | addr q x =>
      simp only [chk] at hc
      split at hc
      · rename_i hx
        simp only [Option.some.injEq] at hc; subst hc
        have hx := List.contains_iff_mem.mp hx
        refine ⟨hI.1, hI.2.1, fun y hy => ?_, fun r y hr => ?_⟩
        · by_cases h : y = x
          · subst h; exact hx
          · simp only [upd_other _ _ h] at hy; exact hI.2.2.1 y hy
        · by_cases h : r = q
          · subst h; simp only [upd_same, Option.some.injEq] at hr; subst hr; exact hx
          · simp only [upd_other _ _ h] at hr; exact hI.2.2.2 r y hr
      · cases hc
    | pset q V m v =>
      simp only [chk, Rules.full, Bool.not_true, Bool.false_or, ite_true] at hc
      split at hc
      · rename_i hl
        simp only [Option.some.injEq] at hc; subst hc
        simp only [exec]
        split
        · trivial
        · rename_i x hx
          exact inv_kill_set (inv_kill hI) (hI.2.2.2 q x hx) ⟨V, m, v⟩ hl
      · cases hc
    | pget y q m =>
      simp [chk, Rules.full] at hc
    | call ws =>
      simp only [chk, Rules.full, Bool.not_true, Bool.false_or, ite_true] at hc
      split at hc
      · rename_i hl
        simp only [Option.some.injEq] at hc; subst hc
        exact inv_callW ws st hl (inv_kill hI)
      · cases hc

theorem inv_init (L : Lay) (G : List Nat) (s : Stmt) :
    Inv L (G ++ addrs s) Abs.init (St.init G) := by
  refine ⟨fun _ => List.mem_singleton.mpr rfl, fun _ h => ?_, fun _ h => ?_, fun _ _ h => ?_⟩
  · cases h
  · exact List.mem_append_left _ (List.contains_iff_mem.mp h)
  · cases h

/-- If the checker accepts `s` under the full rules, then running it from
the zero state with globals `G` never reaches UB, for any fuel: no union
member is ever read while the union holds a member of another type. What is
left is `ok`, `trap` (a null pointer) or `outOfFuel`. -/
theorem tag_sound (L : Lay) (G : List Nat) (s : Stmt) (A : Abs)
    (hc : check Rules.full L G s = some A) (fuel : Nat) :
    (exec L fuel (St.init G) s).isUB = false := by
  have := chk_good L (G ++ addrs s) fuel s Abs.init A (St.init G) hc (inv_init L G s)
  cases ho : exec L fuel (St.init G) s <;> rw [ho] at this <;> first | rfl | exact absurd this id

end SafeC.Tag

-- Lists only propext, Classical.choice and Quot.sound.
#print axioms SafeC.Tag.tag_sound
