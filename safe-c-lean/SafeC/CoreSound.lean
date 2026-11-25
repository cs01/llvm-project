-- The core subset: soundness of the checker.
import SafeC.CoreInv
namespace SafeC.Core

/-! ## Checker states -/

variable {bs : Nat → Bool}

theorem res_path (g : St) (bs x) : (resolve g bs x).path = (g x).path := by
  unfold resolve; split
  · rename_i b e; rw [e]; split <;> rfl
  · rfl

theorem res_root (g : St) (bs x) : rootOf (resolve g bs) x = rootOf g x := by simp [rootOf, res_path]
theorem res_clo (g : St) (bs x) : clo (resolve g bs) x = clo g x := by simp [clo, res_path]

theorem res_lv {g : St} {x} (h : (g x).lv = true) : resolve g bs x = g x := by
  unfold resolve; split
  · rename_i b e; rw [e] at h; cases h
  · rfl

theorem res_bor {g : St} {x} (h : (g x).path ≠ []) : resolve g bs x = g x := by
  unfold resolve; split
  · rename_i b e; rw [e] at h; exact absurd rfl h
  · rfl

theorem res_le {g g' : St} {x} (h : KLe (g' x) (g x)) : KLe (resolve g' bs x) (resolve g bs x) := by
  rcases h with h | h | ⟨c, p, c', p', h1, h2, h3⟩
  · left; simp [resolve, h]
  · right; left; simp [resolve, h]
  · right; right; exact ⟨c, p, c', p', by simp [resolve, h1], by simp [resolve, h2], h3⟩

theorem res_upd {g : St} {x k} (h : k.lv = true) : resolve (upd g x k) bs = upd (resolve g bs) x k := by
  funext y
  by_cases hy : y = x
  · subst hy; rw [res_lv (by rw [upd_same]; exact h), upd_same, upd_same]
  · rw [upd_ne _ _ hy]; unfold resolve; rw [upd_ne _ _ hy]

theorem prune_cases (g : St) (x) : prune g x = .dead ∨
    (prune g x = g x ∧ ((g x).path ≠ [] → (g (rootOf g x)).lv = true ∧ (g (rootOf g x)).path = [] ∧
      ((g x).ex = true → (g (rootOf g x)).ex = true))) := by
  unfold prune
  dsimp only
  split
  · rename_i c p ps e
    split
    · rename_i hc
      right
      refine ⟨e.symm, fun _ => ?_⟩
      simp only [Bool.and_eq_true, Bool.or_eq_true, List.isEmpty_iff, Bool.not_eq_true'] at hc
      have : rootOf g x = ps.getLastD p := by simp [rootOf, e, K.path, List.getLast?_cons]
      rw [this]
      refine ⟨hc.1.1, hc.1.2, fun hx => ?_⟩
      rw [e] at hx
      have : c = true := hx
      rcases hc.2 with h | h
      · simp_all
      · exact h
    · left; rfl
  · right
    refine ⟨rfl, fun h => ?_⟩
    exfalso
    cases e : g x with
    | bor c p =>
      cases p with
      | nil => simp [e, K.path] at h
      | cons a t => rename_i hh; exact hh _ _ _ e
    | _ => simp [e, K.path] at h

theorem prune_keep {g : St} {x} (h : (g x).path = []) : prune g x = g x := by
  unfold prune
  dsimp only
  split
  · rename_i c p ps e; rw [e] at h; cases h
  · rfl

theorem prune_keep' {g : St} {x} (h : (g (rootOf g x)).lv = true ∧ (g (rootOf g x)).path = [] ∧
      ((g x).ex = true → (g (rootOf g x)).ex = true)) : prune g x = g x := by
  unfold prune
  dsimp only
  split
  · rename_i c p ps e
    have hr : rootOf g x = ps.getLastD p := by simp [rootOf, e, K.path, List.getLast?_cons]
    rw [hr, e] at h
    obtain ⟨h1, h2, h3⟩ := h
    cases c <;> simp_all [K.ex]
  · rfl

theorem prune_le (g : St) (x) : KLe (prune g x) (g x) := by
  rcases prune_cases g x with h | h
  · exact .inl h
  · exact .inr (.inl h.1)

theorem rt_prune (g : St) (bs) : Rt (resolve (prune g) bs) := by
  intro x hx
  by_cases hp : (prune g x).path = []
  · have : rootOf (resolve (prune g) bs) x = x := root_of_nil (by rw [res_path, hp])
    rw [this]; exact ⟨hx, by rw [res_path, hp], id⟩
  · rcases prune_cases g x with h | ⟨h, h'⟩
    · rw [h] at hp; exact absurd rfl hp
    · rw [h] at hp
      obtain ⟨h1, h2, h3⟩ := h' hp
      have hr : rootOf (prune g) x = rootOf g x := by simp [rootOf, h]
      rw [res_root, hr]
      have hk := prune_keep h2
      rw [res_lv (by rw [hk]; exact h1), hk]
      refine ⟨h1, h2, fun hx => ?_⟩
      rw [res_bor (by rw [h]; exact hp), h] at hx
      exact h3 hx

theorem shrink_le (q : Nat → Bool) (k : K) : KLe (shrink q k) k := by
  unfold shrink
  split
  · rename_i c p ps
    split
    · exact .inl rfl
    · rename_i l hl
      refine .inr (.inr ⟨c, _, c, _, rfl, rfl, id, ?_, ?_⟩)
      · intro z hz; exact (List.mem_filter.mp hz).1
      · simp only [reduceCtorEq, iff_false]; exact fun e => hl e
  · exact KLe.rfl

theorem shrink_mem {q : Nat → Bool} {k : K} {z} (h : z ∈ (shrink q k).path) : q z = true := by
  unfold shrink at h
  split at h
  · split at h
    · cases h
    · exact (List.mem_filter.mp h).2
  · rename_i hh
    cases k with
    | bor c p => cases p with
      | nil => cases h
      | cons a t => exact absurd rfl (hh _ _ _)
    | _ => cases h

variable {m : Bool}

theorem mutate_full (v : Nat) (g : St) :
    mutate (Rules.full m) v g = prune fun x => if hit g v x then .dead else g x := rfl

theorem moveOut_full (o : Nat) (g : St) : moveOut (Rules.full m) o g = prune (upd g o .dead) := rfl

theorem drop_le (x : Nat) (g : St) (y) : KLe (drop (Rules.full m) x g y) (g y) := by
  refine (prune_le _ _).trans ?_
  split
  · exact .inl rfl
  · exact shrink_le _ _

theorem KLe.sub {k l : K} (h : KLe k l) {z} (hz : z ∈ k.path) : z ∈ l.path := by
  rcases h with rfl | rfl | ⟨c, p, c', p', rfl, rfl, h1, h2, h3⟩
  · cases hz
  · exact hz
  · exact h2 z hz

theorem drop_gone (x : Nat) (g : St) (bs) : Gone (resolve (drop (Rules.full m) x g) bs) x := by
  constructor
  · have : drop (Rules.full m) x g x = .dead := by
      rcases prune_cases (fun y => if y = x then K.dead else shrink (fun z => !(Rules.full m).reparent || z != x) (g y)) x with h | h
      · exact h
      · rw [drop, h.1]; simp
    simp [resolve, this]
  · intro y hy
    rw [res_path] at hy
    have := (prune_le _ _).sub hy
    split at this
    · cases this
    · have := shrink_mem this; simp [Rules.full] at this

theorem moveOut_le (o : Nat) (g : St) (y) : KLe (moveOut (Rules.full m) o g y) (g y) := by
  rw [moveOut_full]
  refine (prune_le _ _).trans ?_
  by_cases h : y = o
  · subst h; rw [upd_same]; exact .inl rfl
  · rw [upd_ne _ _ h]; exact KLe.rfl

theorem mutate_le (v : Nat) (g : St) (y) : KLe (mutate (Rules.full m) v g y) (g y) := by
  rw [mutate_full]
  refine (prune_le _ _).trans ?_
  split
  · exact .inl rfl
  · exact KLe.rfl

theorem rt_mutate (v : Nat) (g : St) (bs) : Rt (resolve (mutate (Rules.full m) v g) bs) := rt_prune _ _
theorem rt_moveOut (v : Nat) (g : St) (bs) : Rt (resolve (moveOut (Rules.full m) v g) bs) := rt_prune _ _
theorem rt_drop (v : Nat) (g : St) (bs) : Rt (resolve (drop (Rules.full m) v g) bs) := rt_prune _ _

theorem mutate_hd {v : Nat} {g : St} {y} (h : (resolve (mutate (Rules.full m) v g) bs y).lv = true) :
    y ∈ clo g v ∨ rootOf g y ≠ rootOf g v := by
  refine Classical.byContradiction fun hn => ?_
  have hh : hit g v y = true := by
    simp only [not_or, Classical.not_not] at hn
    simp [hit, hn.2, hn.1]
  have : mutate (Rules.full m) v g y = .dead := by
    rcases prune_cases (fun x => if hit g v x then K.dead else g x) y with e | e
    · simpa [mutate, Rules.full] using e
    · have := e.1; simpa [mutate, Rules.full, hh] using this
  simp [resolve, this, K.lv] at h

theorem moveOut_hd {o : Nat} {g : St} {y} (h : (resolve (moveOut (Rules.full m) o g) bs y).lv = true) :
    rootOf g y ≠ o := by
  intro hr
  have : moveOut (Rules.full m) o g y = .dead := by
    rw [moveOut_full]
    by_cases hy : y = o
    · subst hy
      rw [prune_keep] <;> rw [upd_same]
      rfl
    · rcases prune_cases (upd g o .dead) y with e | ⟨e, e'⟩
      · exact e
      · by_cases hp : (g y).path = []
        · exact absurd ((root_of_nil hp).symm.trans hr) hy
        · rw [upd_ne _ _ hy, rootOf_upd hy, hr, upd_same] at e'
          cases (e' hp).1
  simp [resolve, this, K.lv] at h

theorem mutOk_ex {g : St} {v} (h : mutOk (Rules.full m) g v = true) :
    (g v).ex = true ∧ (g (rootOf g v)).lv = true := by
  simpa [mutOk, Rules.full] using h

variable {np : Nat} {env0 : Nat → Option Nat} {F : Nat → Prop} {H0 : Heap} {g : St}
  {env : Nat → Option Nat} {h : Heap}

theorem rt_un (I : Inv np env0 F H0 (resolve g bs) env h) {v} (hv : (g v).lv = true)
    (hr : (g (rootOf g v)).lv = true) :
    (g (rootOf g v)).lv = true ∧ (g (rootOf g v)).path = [] ∧ ((g v).ex = true → (g (rootOf g v)).ex = true) := by
  have := I.rt v (by rw [res_lv hv]; exact hv)
  rwa [res_root, res_lv hr, res_lv hv] at this

theorem mutate_self (I : Inv np env0 F H0 (resolve g bs) env h) {v}
    (hv : mutOk (Rules.full m) g v = true) : mutate (Rules.full m) v g v = g v := by
  obtain ⟨h1, h2⟩ := mutOk_ex hv
  have r := rt_un I (ex_lv h1) h2
  rw [mutate_full]
  have e1 : ∀ z ∈ clo g v, (if hit g v z then K.dead else g z) = g z := by
    intro z hz; simp [hit, hz]
  have hself := e1 v (by simp [clo])
  have hroot := e1 _ (root_clo g v)
  have e2 : rootOf (fun x => if hit g v x then K.dead else g x) v = rootOf g v := by
    simp only [rootOf, hself]
  rw [prune_keep']
  · exact hself
  · simp only [e2, hself, hroot]; exact r

theorem touch_F (I : Inv np env0 F H0 g env h) {E : Nat → Prop} (hE : ∀ e, E e → (g e).ex = true) :
    ∀ n, Touch env h E n → F n := by
  rintro n (⟨e, a, h1, h2, h3⟩ | h1)
  · obtain ⟨r1, r2, r3⟩ := I.rt e (ex_lv (hE e h1))
    obtain ⟨b, eb, rb⟩ := I.anc' (ex_lv (hE e h1)) (root_clo g e) h2
    have := I.inF _ _ r1 r2 (r3 (hE e h1)) eb
    have r := rb.trans h3
    clear h3 rb eb h2
    induction r with
    | refl => exact this
    | step e' _ ih => exact ih (I.Fcl _ _ _ this e')
  · exact I.fresh _ (Nat.le_trans I.nx h1)

theorem pOk_le {x} (h : pOk (Rules.full m) np x = true) : np ≤ x := by
  simpa [pOk, Rules.full] using h

/-! ## Assignments -/

theorem grd_some {α} {b : Bool} {r : Option α} {x : α} : grd b r = some x ↔ b = true ∧ r = some x := by
  cases b <;> simp [grd]

theorem Inv.toDrop (I : Inv np env0 F H0 (resolve g bs) env h) (x : Nat) :
    Inv np env0 F H0 (resolve (drop (Rules.full m) x g) bs) env h :=
  I.mono (fun y => res_le (drop_le x g y)) (rt_drop _ _ _)

theorem Inv.assignOwn (I : Inv np env0 F H0 (resolve g bs) env h) {x : Nat}
    (hx : pOk (Rules.full m) np x = true) (v : Option Nat)
    (H : ∀ a, v = some a → h.alive a = true ∧ F a ∧ (∀ b f, h.fld b f ≠ some a) ∧
      ∀ y b, (resolve g bs y).lv = true → (g y).path = [] → env y = some b → Disj h a b) :
    Inv np env0 F H0 (resolve (upd (drop (Rules.full m) x g) x .own) bs) (upd env x v) h := by
  rw [res_upd rfl]
  refine (I.toDrop x).addOwn (pOk_le hx) (drop_gone _ _ _) v fun a e => ?_
  obtain ⟨h1, h2, h3, h4⟩ := H a e
  refine ⟨h1, h2, h3, fun y b hy hp hb => ?_⟩
  obtain ⟨y1, _, _, y4, _⟩ := (res_le (bs := bs) (drop_le (m := m) x g y)).facts hy
  exact h4 y b y1 (by rw [← res_path g bs]; exact y4.mp hp) hb

theorem getLastD_filter {l : List Nat} {q : Nat → Bool} {d : Nat} (h : l ≠ [] → q (l.getLastD d) = true) :
    (l.filter q).getLastD d = l.getLastD d := by
  rcases List.eq_nil_or_concat l with rfl | ⟨l', b, rfl⟩
  · rfl
  · have : q b = true := by simpa using h (by simp)
    simp [List.filter_append, this]

theorem borrow_ok (R : Rules) (hR : R.noParamAssign = true ∧ R.reparent = true ∧ R.capInherit = true)
    (I : Inv np env0 F H0 (resolve g bs) env h) {x v : Nat} {mo : Mode} {g' : St} (val : Option Nat)
    (hb : borrowK R np x mo v g = some g')
    (hv : ∀ a, val = some a → ∃ b, env v = some b ∧ Reach h b a) :
    Inv np env0 F H0 (resolve g' bs) (upd env x val) h := by
  have hd : drop R x g = drop (Rules.full true) x g := by
    funext y; simp [drop, Rules.full, hR.2.1]
  obtain ⟨hc, hb⟩ := grd_some.mp hb
  cases hb
  simp only [Bool.and_eq_true, Bool.not_eq_true', hR.2.1, hR.2.2, pOk, hR.1, Bool.not_true, Bool.false_or,
    decide_eq_true_eq, bne_iff_ne, ne_eq] at hc ⊢
  obtain ⟨⟨⟨hx, hl⟩, hne⟩, hpe⟩ := hc
  rw [hd]
  generalize hpd : (if x = v then [] else [v]) ++ (g v).path.filter (fun z => z != x) = p at hpe ⊢
  rw [res_upd rfl]
  have hpne : p ≠ [] := by intro e; simp [e] at hpe
  have hG : resolve g bs v = g v := res_lv hl
  have hsub : ∀ q ∈ p, q ≠ x ∧ q ∈ clo g v := by
    intro q hq
    rw [← hpd] at hq
    rcases List.mem_append.mp hq with h1 | h1
    · split at h1
      · cases h1
      · rename_i n; simp at h1; subst h1; exact ⟨Ne.symm n, by simp [clo]⟩
    · rw [List.mem_filter] at h1
      exact ⟨by simpa using h1.2, List.mem_cons_of_mem _ h1.1⟩
  -- the last ancestor is v's root
  have hlast : p.getLastD x = rootOf g v := by
    have hf : ∀ d, (g v).path ≠ [] → ((g v).path.filter (fun z => z != x)).getLastD d = rootOf g v := by
      intro d hp
      rcases List.eq_nil_or_concat (g v).path with e | ⟨l', b, e⟩
      · exact absurd e hp
      · have hb : rootOf g v = b := by simp [rootOf, e]
        have : b ≠ x := fun h => hne (hb.trans h)
        rw [hb, e]; simp [List.filter_append, this]
    have hfe : (g v).path ≠ [] → (g v).path.filter (fun z => z != x) ≠ [] := by
      intro hp e
      rcases List.eq_nil_or_concat (g v).path with e' | ⟨l', b, e'⟩
      · exact hp e'
      · have hb : rootOf g v = b := by simp [rootOf, e']
        have : b ≠ x := fun h => hne (hb.trans h)
        rw [e'] at e; simp [List.filter_append, this] at e
    by_cases hp : (g v).path = []
    · have hr : rootOf g v = v := root_of_nil hp
      rw [← hpd, hp]
      split
      · rename_i e; exact absurd (hr.trans e.symm) hne
      · simp [hr]
    · rw [← hpd]
      split
      · simpa using hf x hp
      · rw [List.cons_append, List.nil_append]
        have := hf v hp
        rcases List.eq_nil_or_concat ((g v).path.filter fun z => z != x) with e | ⟨l', b, e⟩
        · exact absurd e (hfe hp)
        · rw [e] at this ⊢; simpa [List.getLast?_cons] using this
  have hRt := I.rt v (by rw [hG]; exact hl)
  rw [res_root, hG] at hRt
  -- v's root survives the drop
  have hroot : resolve (drop (Rules.full true) x g) bs (rootOf g v) = resolve g bs (rootOf g v) := by
    have hp0 : (g (rootOf g v)).path = [] := by rw [← res_path g bs]; exact hRt.2.1
    have : drop (Rules.full true) x g (rootOf g v) = g (rootOf g v) := by
      have hs : ∀ q, shrink q (g (rootOf g v)) = g (rootOf g v) := by
        intro q
        unfold shrink
        split
        · rename_i e; rw [e] at hp0; cases hp0
        · rfl
      unfold drop
      rw [prune_keep] <;> simp [hne, hs, hp0]
    unfold resolve; rw [this]
  refine (I.toDrop x).add hx (drop_gone _ _ _) _ val rfl ?_ ?_ ?_ ?_ ?_
  · show _ ∧ _ ∧ _
    simp only [K.path, hlast]
    rw [upd_ne _ _ hne, hroot]
    refine ⟨hRt.1, hRt.2.1, fun he => hRt.2.2 ?_⟩
    simp only [K.ex, Bool.and_eq_true] at he
    exact he.2
  · exact fun hq => (hsub x hq).1 rfl
  · intro a e
    obtain ⟨b, hb, rb⟩ := hv a e
    exact I.wf.ral rb (I.al v b (by rw [hG]; exact hl) hb)
  · intro a q e hq
    obtain ⟨b, hb, rb⟩ := hv a e
    obtain ⟨c, hc, rc⟩ := I.anc' (q := q) (by rw [hG]; exact hl)
      (by rw [res_clo]; exact (hsub q hq).2) hb
    exact ⟨c, hc, rc.trans rb⟩
  · intro hp; exact absurd hp hpne

/-! ## Booleans and joins -/

theorem untie_le (b : Nat) (v : Bool) (g : St) (x) :
    KLe (resolve (untie (Rules.full m) b g) (upd bs b v) x) (resolve g bs x) := by
  have h1 := res_le (bs := upd bs b v) (g := fun x => if (Rules.full m).untie && g x == .maybe b then K.dead else g x) (prune_le (fun x => if (Rules.full m).untie && g x == .maybe b then K.dead else g x) x)
  refine h1.trans ?_
  by_cases hb : g x = .maybe b
  · left; simp [resolve, Rules.full, hb]
  · right; left
    simp only [resolve, Rules.full, hb, Bool.true_and, beq_iff_eq, ite_false]
    cases e : g x with
    | maybe b' =>
      have : b' ≠ b := by rintro rfl; exact hb e
      simp [upd_ne _ _ this]
    | _ => rfl

theorem untie_ok (I : Inv np env0 F H0 (resolve g bs) env h) (b : Nat) (v : Bool) :
    Inv np env0 F H0 (resolve (untie (Rules.full m) b g) (upd bs b v)) env h :=
  I.mono (untie_le b v g) (rt_prune _ _)

theorem res_assume {b v} (hb : bs b = v) : resolve (assume b v g) bs = resolve g bs := by
  funext x
  unfold resolve assume
  by_cases e : g x = .maybe b
  · simp only [e, ite_true]
    cases v <;> simp [hb]
  · simp [e]

theorem joinK_le (k l : K) : KLe (joinK k l) k ∧ KLe (joinK k l) l := by
  unfold joinK
  split
  · rename_i c p c' p'
    split
    · exact ⟨.inl rfl, .inl rfl⟩
    · rename_i hp
      have hpe : p = [] ↔ p' = [] := by
        cases p <;> cases p' <;> simp_all
      have S := shrink_le (fun z => p'.contains z) (.bor (c && c') p)
      refine ⟨S.trans (.inr (.inr ⟨_, _, _, _, rfl, rfl, by simp; grind, fun _ => id, Iff.rfl⟩)), ?_⟩
      cases p with
      | nil =>
        cases hpe.mp rfl
        exact .inr (.inr ⟨_, _, _, _, rfl, rfl, by simp, by simp, Iff.rfl⟩)
      | cons a t =>
        have hp' : p' ≠ [] := fun e => by cases hpe.mpr e
        unfold shrink
        split
        · rename_i hh; cases hh
          split
          · exact .inl rfl
          · rename_i l hl
            refine .inr (.inr ⟨_, _, _, _, rfl, rfl, by simp, ?_, ?_⟩)
            · intro z hz; simpa using (List.mem_filter.mp hz).2
            · exact ⟨fun e => absurd e hl, fun e => absurd e hp'⟩
        · rename_i hh; exact absurd rfl (hh _ _ _)
  · split
    · rename_i e; subst e; exact ⟨KLe.rfl, KLe.rfl⟩
    · exact ⟨.inl rfl, .inl rfl⟩

theorem join_l (g1 g2 : St) (I : Inv np env0 F H0 (resolve g1 bs) env h) :
    Inv np env0 F H0 (resolve (join g1 g2) bs) env h :=
  I.mono (fun x => res_le ((prune_le _ _).trans (joinK_le (g1 x) (g2 x)).1)) (rt_prune _ _)

theorem join_r (g1 g2 : St) (I : Inv np env0 F H0 (resolve g2 bs) env h) :
    Inv np env0 F H0 (resolve (join g1 g2) bs) env h :=
  I.mono (fun x => res_le ((prune_le _ _).trans (joinK_le (g1 x) (g2 x)).2)) (rt_prune _ _)

theorem kle_le {k l : K} (hk : kle k l = true) : KLe k l := by
  unfold kle at hk
  split at hk
  · exact .inl rfl
  · rename_i c p c' p'
    simp only [Bool.and_eq_true, Bool.or_eq_true, Bool.not_eq_true', List.all_eq_true,
      List.contains_iff_mem, beq_iff_eq] at hk
    refine .inr (.inr ⟨_, _, _, _, rfl, rfl, ?_, hk.1.2, ?_⟩)
    · intro e; rcases hk.1.1 with h | h <;> simp_all
    · have := hk.2
      cases p <;> cases p' <;> simp_all
  · simp at hk; exact .inr (.inl hk)

theorem trunc_ge {n x} (hx : ¬ x < n) (g : St) : trunc n g x = .dead := by
  have h0 : ((List.range n).map g).getD x .dead = .dead := by
    simp [List.getD, hx]
  rcases prune_le (fun x => ((List.range n).map g).getD x .dead) x with e | e | ⟨c, p, c', p', e1, e2, _⟩
  · exact e
  · exact e.trans h0
  · rw [h0] at e2; cases e2

theorem stLe_ok {n : Nat} {g0 : St} (hl : stLe n (trunc n g0) g = true)
    (I : Inv np env0 F H0 (resolve g bs) env h) :
    Inv np env0 F H0 (resolve (trunc n g0) bs) env h := by
  refine I.mono (fun x => res_le ?_) (rt_prune _ _)
  by_cases hx : x < n
  · simp only [stLe, List.all_eq_true, List.mem_range] at hl
    exact kle_le (hl x hx)
  · exact .inl (trunc_ge hx g0)

/-! ## Heap changes -/

theorem setf_edge {h : Heap} {a f v n k c} (e : (h.setf a f v).fld n k = some c) :
    h.fld n k = some c ∨ (n = a ∧ k = f ∧ v = some c) := by
  simp only [Heap.setf, upd] at e
  by_cases h1 : n = a
  · by_cases h2 : k = f
    · subst h1 h2; simp only [upd, ite_eq_left] at e; exact .inr ⟨rfl, rfl, e⟩
    · simp only [h1, upd, ite_true, ite_eq_right h2] at e; exact .inl (h1 ▸ e)
  · simp [h1] at e; exact .inl e

theorem setf_other {h : Heap} {a f v n k} (hn : n ≠ a) : (h.setf a f v).fld n k = h.fld n k := by
  simp [Heap.setf, upd_ne _ _ hn]

theorem setf_reach {h : Heap} {a f o m n} (p : Reach (h.setf a f (some o)) m n) :
    Reach h m n ∨ (Reach h m a ∧ Reach h o n) := by
  induction p with
  | refl => exact .inl (.refl _)
  | step e _ ih =>
    rcases setf_edge e with e | ⟨rfl, rfl, q⟩
    · rcases ih with h1 | ⟨h1, h2⟩
      · exact .inl (.step e h1)
      · exact .inr ⟨.step e h1, h2⟩
    · cases q
      rcases ih with h1 | ⟨h1, h2⟩
      · exact .inr ⟨.refl _, h1⟩
      · exact .inr ⟨.refl _, h2⟩

theorem setf_none_reach {h : Heap} {a f m n} (p : Reach (h.setf a f none) m n) : Reach h m n := by
  refine p.mono fun x k c e => ?_
  rcases setf_edge e with e | ⟨_, _, q⟩
  · exact e
  · cases q

theorem wf_take {h : Heap} (w : WF h) (a f) : WF (h.setf a f none) := by
  have E : ∀ {n k c}, (h.setf a f none).fld n k = some c → h.fld n k = some c := by
    intro n k c e
    rcases setf_edge e with e | ⟨_, _, q⟩
    · exact e
    · cases q
  exact
    { lt := fun _ _ _ e => w.lt _ _ _ (E e), alt := w.alt, alc := fun _ _ _ ha e => w.alc _ _ _ ha (E e)
      uniq := fun _ _ _ _ _ e1 e2 => w.uniq _ _ _ _ _ (E e1) (E e2)
      acyc := fun _ _ _ e p => w.acyc _ _ _ (E e) (setf_none_reach p) }

theorem wf_store {h : Heap} (w : WF h) {a f o} (ha : h.alive a = true) (ho : h.alive o = true)
    (hp : ∀ b k, h.fld b k ≠ some o) (hd : ¬ Reach h o a) : WF (h.setf a f (some o)) := by
  refine { lt := ?_, alt := w.alt, alc := ?_, uniq := ?_, acyc := ?_ }
  · intro n k c e
    rcases setf_edge e with e | ⟨rfl, rfl, q⟩
    · exact w.lt _ _ _ e
    · cases q; exact ⟨w.alt _ ha, w.alt _ ho⟩
  · intro n k c hn e
    rcases setf_edge e with e | ⟨rfl, rfl, q⟩
    · exact w.alc _ _ _ hn e
    · cases q; exact ho
  · intro n k n' k' c e e'
    rcases setf_edge e with e | ⟨rfl, rfl, q⟩
    · rcases setf_edge e' with e' | ⟨rfl, rfl, q'⟩
      · exact w.uniq _ _ _ _ _ e e'
      · cases q'; rename_i e1; exact absurd e1 (hp _ _)
    · cases q
      rcases setf_edge e' with e' | ⟨rfl, rfl, q'⟩
      · exact absurd e' (hp _ _)
      · exact ⟨rfl, rfl⟩
  · intro n k c e p
    rcases setf_edge e with e | ⟨rfl, rfl, q⟩
    · rcases setf_reach p with h1 | ⟨h1, h2⟩
      · exact w.acyc _ _ _ e h1
      · exact hd (h2.trans (.step e h1))
    · cases q
      rcases setf_reach p with h1 | ⟨h1, _⟩
      · exact hd h1
      · exact hd h1

theorem wf_alloc {h : Heap} (w : WF h) : WF h.alloc := by
  refine { lt := ?_, alt := ?_, alc := ?_, uniq := w.uniq, acyc := ?_ }
  · intro n k c e
    have := w.lt n k c e
    simp only [Heap.alloc]; omega
  · intro n hn
    simp only [Heap.alloc] at hn ⊢
    by_cases e : n = h.next
    · omega
    · rw [upd_ne _ _ e] at hn; have := w.alt _ hn; omega
  · intro n k c hn e
    have hl := w.lt n k c e
    simp only [Heap.alloc] at hn ⊢
    rw [upd_ne _ _ (by omega)] at hn ⊢
    exact w.alc _ _ _ hn e
  · intro n k c e p
    exact w.acyc n k c e (p.mono (h := h) (h' := h.alloc) fun _ _ _ e => e)

theorem wf_kill {h : Heap} (w : WF h) {a} (hp : ∀ b k, h.fld b k ≠ some a) : WF (h.kill a) := by
  refine { lt := w.lt, alt := ?_, alc := ?_, uniq := w.uniq, acyc := ?_ }
  · intro n hn
    simp only [Heap.kill] at hn
    by_cases e : n = a
    · subst e; rw [upd_same] at hn; cases hn
    · rw [upd_ne _ _ e] at hn; exact w.alt _ hn
  · intro n k c hn e
    simp only [Heap.kill] at hn ⊢
    by_cases e' : n = a
    · subst e'; rw [upd_same] at hn; cases hn
    · rw [upd_ne _ _ e'] at hn
      have : c ≠ a := fun x => hp n k (x ▸ e)
      rw [upd_ne _ _ this]; exact w.alc _ _ _ hn e
  · intro n k c e p
    exact w.acyc n k c e (p.mono (h := h) (h' := h.kill a) fun _ _ _ e => e)

theorem free_ok (I : Inv np env0 F H0 (resolve g bs) env h) {o a} (ho : g o = .own) (ha : env o = some a) :
    Inv np env0 F H0 (resolve (moveOut (Rules.full m) o g) bs) env (h.kill a) := by
  have hG : resolve g bs o = .own := by rw [res_lv (by rw [ho]; rfl), ho]
  have hro : rootOf g o = o := root_of_nil (by rw [ho]; rfl)
  have hdead : (resolve (moveOut (Rules.full m) o g) bs o).lv ≠ true := fun hl => moveOut_hd hl hro
  refine I.touch (fun e => e = o) ?_ (wf_kill I.wf fun b k => I.par o a b k hG ha) (Nat.le_refl _) ?_ ?_
    (fun x => res_le (moveOut_le o g x)) (rt_moveOut _ _ _) ?_ ?_ ?_ ?_
  · rintro e rfl; rw [hG]; rfl
  · intro n hn
    have : n ≠ a := by rintro rfl; exact hn (.inl ⟨o, n, rfl, ha, .refl _⟩)
    simp [Heap.kill, upd_ne _ _ this]
  · exact touch_cl I.wf
  · rintro y e rfl hy
    right; rw [res_root, res_root, hro]; exact moveOut_hd hy
  · rintro e rfl; exact .inr hG
  · rintro e a' rfl hl; exact absurd hl hdead
  · rintro e1 e2 _ _ rfl _ _ hl; exact absurd hl hdead

theorem move_ok (I : Inv np env0 F H0 (resolve g bs) env h) (o : Nat) :
    Inv np env0 F H0 (resolve (moveOut (Rules.full m) o g) bs) env h :=
  I.mono (fun x => res_le (moveOut_le o g x)) (rt_moveOut _ _ _)

theorem store_ok (I : Inv np env0 F H0 (resolve g bs) env h) {v o a f} (ho : g o = .own)
    (hv : mutOk (Rules.full m) g v = true) (hr : rootOf g v ≠ o) (ha : env v = some a) :
    Inv np env0 F H0 (resolve (moveOut (Rules.full m) o (mutate (Rules.full m) v g)) bs) env
      (h.setf a f (env o)) := by
  obtain ⟨hex, hrl⟩ := mutOk_ex hv
  have hG : resolve g bs o = .own := by rw [res_lv (by rw [ho]; rfl), ho]
  have hGv : resolve g bs v = g v := res_lv (ex_lv hex)
  have hro : rootOf g o = o := root_of_nil (by rw [ho]; rfl)
  have hrm : rootOf (mutate (Rules.full m) v g) o = o := by
    refine root_of_nil ?_
    cases e : (mutate (Rules.full m) v g o).path with
    | nil => rfl
    | cons z t =>
      have := (mutate_le (m := m) v g o).sub (z := z) (by rw [e]; simp)
      rw [ho] at this; cases this
  have hdead : (resolve (moveOut (Rules.full m) o (mutate (Rules.full m) v g)) bs o).lv ≠ true :=
    fun hl => moveOut_hd hl hrm
  have hal : h.alive a = true := I.al v a (by rw [hGv]; exact ex_lv hex) ha
  have dis : ∀ b, env o = some b → Disj h a b := fun b hb =>
    I.disj (x := v) (y := o) (by rw [hGv]; exact ex_lv hex) (by rw [hGv]; exact hex) (by rw [hG]; rfl)
      (by rw [res_root, res_root, hro]; exact hr) ha hb
  have edge : ∀ n k c, (h.setf a f (env o)).fld n k = some c → h.fld n k = some c ∨ (n = a ∧ env o = some c) := by
    intro n k c e
    rcases setf_edge e with e | ⟨e1, _, e2⟩
    · exact .inl e
    · exact .inr ⟨e1, e2⟩
  have vsub : ∀ e, e = v ∨ e = o → e ≠ o → e = v := by rintro e (rfl | rfl) h; rfl; exact absurd rfl h
  refine I.touch (h' := h.setf a f (env o)) (fun e => e = v ∨ e = o) ?_ ?_ (Nat.le_refl _) ?_ ?_
    (fun x => res_le ((moveOut_le (m := m) o _ x).trans (mutate_le (m := m) v g x))) (rt_moveOut _ _ _) ?_ ?_ ?_ ?_
  · rintro e (rfl | rfl)
    · rw [hGv]; exact hex
    · rw [hG]; rfl
  · cases hb : env o with
    | none => exact wf_take I.wf _ _
    | some b =>
      exact wf_store I.wf hal (I.al o b (by rw [hG]; rfl) hb) (fun c k => I.par o b c k hG hb)
        (fun p => dis b hb a (.refl _) p)
  · intro n hn
    have : n ≠ a := by rintro rfl; exact hn (.inl ⟨v, n, .inl rfl, ha, .refl _⟩)
    exact ⟨rfl, fun k => setf_other this⟩
  · intro n k c hn e
    rcases edge n k c e with q | ⟨rfl, q⟩
    · exact touch_cl I.wf n k c hn q
    · exact .inl ⟨o, c, .inr rfl, q, .refl _⟩
  · rintro y e (rfl | rfl) hy
    · rw [res_root, res_root, res_clo]
      exact mutate_hd (m := m) (bs := bs) ((res_le (moveOut_le (m := m) o _ y)).facts hy).1
    · right
      rw [res_root, res_root, hro]
      have hl := ((res_le (bs := bs) (moveOut_le (m := m) e (mutate (Rules.full m) v g) y)).facts hy).1
      have hne := moveOut_hd hy
      intro hyr; apply hne
      -- mutate keeps paths of survivors
      have : mutate (Rules.full m) v g y = g y := by
        rw [mutate_full] at hl ⊢
        rcases prune_cases (fun x => if hit g v x then K.dead else g x) y with q | q
        · simp [resolve, q, K.lv] at hl
        · have q1 := q.1
          by_cases hh : hit g v y = true
          · simp [hh] at q1; simp [resolve, q1, K.lv] at hl
          · simpa [hh] using q1
      rw [← hyr]; simp [rootOf, this]
  · rintro e (rfl | rfl)
    · left
      have h1 := mutate_self I hv
      have hc : rootOf (mutate (Rules.full m) e g) e = rootOf g e := by simp [rootOf, h1]
      have hrk : mutate (Rules.full m) e g (rootOf g e) = g (rootOf g e) := by
        have r := rt_un I (ex_lv hex) hrl
        rw [mutate_full]
        have : hit g e (rootOf g e) = false := by simp [hit, root_clo]
        rw [prune_keep] <;> simp [this, r.2.1]
      have heo : e ≠ o := fun q => hr (by rw [q, hro])
      have hk : moveOut (Rules.full m) o (mutate (Rules.full m) e g) e = g e := by
        rw [moveOut_full, prune_keep', upd_ne _ _ heo, h1]
        rw [rootOf_upd heo, hc, upd_ne _ _ hr, upd_ne _ _ heo, hrk, h1]
        exact rt_un I (ex_lv hex) hrl
      rw [res_lv (by rw [hk]; exact ex_lv hex), hk]; exact ex_lv hex
    · exact .inr hG
  · rintro e a' he hl ha'
    have hev : e = v := vsub e he (by rintro rfl; exact hdead hl)
    subst hev
    rw [ha] at ha'; cases ha'
    refine ⟨hal, fun b k hb e' => ?_⟩
    rcases edge b k a e' with q | ⟨rfl, q⟩
    · rcases hb with ⟨l, d, hl, h2, h3⟩ | h1
      · rcases hl with rfl | rfl
        · rw [ha] at h2; cases h2; exact I.wf.acyc _ _ _ q h3
        · exact dis d h2 a (.refl _) (h3.trans (.one q))
      · have := (I.wf.lt _ _ _ q).1; omega
    · exact dis _ q _ (.refl _) (.refl _)
  · rintro e1 e2 _ _ h1 h2 hne l1 l2
    have q1 : e1 = v := vsub e1 h1 (by rintro rfl; exact hdead l1)
    have q2 : e2 = v := vsub e2 h2 (by rintro rfl; exact hdead l2)
    exact absurd (q1.trans q2.symm) hne

theorem take_ok (I : Inv np env0 F H0 (resolve g bs) env h) {x v a f}
    (hx : pOk (Rules.full m) np x = true)
    (hv : mutOk (Rules.full m) g v = true) (ha : env v = some a) :
    Inv np env0 F H0 (resolve (upd (drop (Rules.full m) x (mutate (Rules.full m) v g)) x .own) bs)
      (upd env x (h.fld a f)) (h.setf a f none) := by
  obtain ⟨hex, hrl⟩ := mutOk_ex hv
  have hGv : resolve g bs v = g v := res_lv (ex_lv hex)
  have hvl : (resolve g bs v).lv = true := by rw [hGv]; exact ex_lv hex
  have hal : h.alive a = true := I.al v a hvl ha
  have sub : ∀ n k c, (h.setf a f none).fld n k = some c → h.fld n k = some c ∧ ¬ (n = a ∧ k = f) := by
    intro n k c e
    refine ⟨?_, ?_⟩
    · rcases setf_edge e with e | ⟨_, _, q⟩
      · exact e
      · cases q
    · rintro ⟨rfl, rfl⟩; simp [Heap.setf, upd_same] at e
  have hEe : ∀ e, e = v → (resolve g bs e).ex = true := by rintro e rfl; rw [hGv]; exact hex
  have J : Inv np env0 F H0 (resolve (mutate (Rules.full m) v g) bs) env (h.setf a f none) := by
    refine I.touch (fun e => e = v) hEe (wf_take I.wf _ _) (Nat.le_refl _) ?_ ?_
      (fun x => res_le (mutate_le v g x)) (rt_mutate _ _ _) ?_ ?_ ?_ ?_
    · intro n hn
      have : n ≠ a := by rintro rfl; exact hn (.inl ⟨v, n, rfl, ha, .refl _⟩)
      exact ⟨rfl, fun k => setf_other this⟩
    · intro n k c hn e; exact touch_cl I.wf n k c hn (sub _ _ _ e).1
    · rintro y e rfl hy
      rw [res_root, res_root, res_clo]; exact mutate_hd hy
    · rintro e rfl
      left
      have := mutate_self I hv
      rw [res_lv (by rw [this]; exact ex_lv hex), this]; exact ex_lv hex
    · rintro e a' rfl _ ha'
      rw [ha] at ha'; cases ha'
      refine ⟨hal, fun b k hb e' => ?_⟩
      rcases hb with ⟨l, d, rfl, h2, h3⟩ | h1
      · rw [ha] at h2; cases h2; exact I.wf.acyc _ _ _ (sub _ _ _ e').1 h3
      · have := (I.wf.lt _ _ _ (sub _ _ _ e').1).1; omega
    · rintro e1 e2 _ _ rfl rfl hne; exact absurd rfl hne
  refine J.assignOwn hx _ fun c hc => ?_
  have noP : ∀ b k, (h.setf a f none).fld b k ≠ some c := by
    intro b k e
    obtain ⟨e1, e2⟩ := sub _ _ _ e
    exact e2 (I.wf.uniq _ _ _ _ _ e1 hc)
  refine ⟨I.wf.alc _ _ _ hal hc, touch_F I hEe c (.inl ⟨v, a, rfl, ha, .one hc⟩), noP, ?_⟩
  intro y b hy hp hb d p q
  obtain ⟨y1, _, _, y4, _⟩ := (res_le (bs := bs) (mutate_le (m := m) v g y)).facts hy
  have hyp : (resolve g bs y).path = [] := y4.mp (by rw [res_path]; exact hp)
  have p0 := setf_none_reach p
  have q0 := setf_none_reach q
  by_cases hr : rootOf (resolve g bs) v = rootOf (resolve g bs) y
  · -- y is v's root, above the detached object
    rw [root_of_nil hyp] at hr
    obtain ⟨b', e1, e2⟩ := I.anc' hvl (root_clo _ v) ha
    rw [hr, hb] at e1; cases e1
    rcases J.wf.up q p with k | k
    · rcases k.tail with rfl | ⟨b'', f'', _, e''⟩
      · exact I.wf.acyc _ _ _ hc e2
      · exact noP _ _ e''
    · exact I.wf.acyc _ _ _ hc ((setf_none_reach k).trans e2)
  · exact I.disj hvl (hEe v rfl) y1 hr ha hb d (.step hc p0) q0

theorem alloc_ok (I : Inv np env0 F H0 (resolve g bs) env h) {x} (hx : pOk (Rules.full m) np x = true) :
    Inv np env0 F H0 (resolve (upd (drop (Rules.full m) x g) x .own) bs) (upd env x (some h.next)) h.alloc := by
  have nt : ∀ n, ¬ Touch env h (fun _ => False) n → n ≠ h.next := by
    rintro n hn rfl; exact hn (.inr (Nat.le_refl _))
  have J : Inv np env0 F H0 (resolve (drop (Rules.full m) x g) bs) env h.alloc := by
    refine (I.toDrop x).touch (fun _ => False) (fun _ h => h.elim) (wf_alloc I.wf) (by simp [Heap.alloc]) ?_ ?_
      (fun x => KLe.rfl) (rt_drop _ _ _) (fun _ _ h => h.elim) (fun _ h => h.elim) (fun _ _ h => h.elim)
      (fun _ _ _ _ h => h.elim)
    · intro n hn
      exact ⟨by simp [Heap.alloc, upd_ne _ _ (nt n hn)], fun _ => rfl⟩
    · exact touch_cl I.wf
  have h0 : ∀ c, Reach h.alloc h.next c → c = h.next := by
    intro c p
    cases p with
    | refl => rfl
    | step e _ => have := (I.wf.lt _ _ _ e).1; omega
  rw [res_upd rfl]
  refine J.addOwn (pOk_le hx) (drop_gone _ _ _) _ fun a e => ?_
  cases e
  refine ⟨by simp [Heap.alloc, upd_same], I.fresh _ I.nx, ?_, ?_⟩
  · intro b f e; have := (I.wf.lt _ _ _ e).2; omega
  · intro y b hy _ hb c p q
    have := h0 c p; subst this
    have hb' := I.wf.alt _ ((I.toDrop (m := m) x).al y b hy hb)
    have := I.wf.rlt (q.mono (h := h) (h' := h.alloc) fun _ _ _ e => e) hb'
    omega

end SafeC.Core
