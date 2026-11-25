-- The core subset: calls (rules 9 and 10) and the main theorem.
import SafeC.CoreSound
namespace SafeC.Core

variable {m : Bool} {bs : Nat → Bool} {np : Nat} {env0 : Nat → Option Nat} {F : Nat → Prop} {H0 : Heap}
  {g : St} {env : Nat → Option Nat} {h : Heap}

/-! ## Entering a callee -/

/-- The argument locals the callee may mutate through. -/
def Args (args : List Nat) (ps : List PK) (e : Nat) : Prop :=
  ∃ (i : Nat) (k : PK), args[i]? = some e ∧ ps[i]? = some k ∧ k ≠ .shr

structure ArgsOk (m : Bool) (g : St) (args : List Nat) (ps : List PK) : Prop where
  len : args.length = ps.length
  ok : ∀ (i v : Nat) (k : PK), args[i]? = some v → ps[i]? = some k → argOk (Rules.full m) g v k = true
  roots : ∀ (i j v w : Nat) (k l : PK), args[i]? = some v → ps[i]? = some k → args[j]? = some w → ps[j]? = some l →
    i ≠ j → k ≠ .shr → rootOf g v ≠ rootOf g w

theorem argsOk_of {args : List Nat} {ps : List PK}
    (hc : (decide (args.length = ps.length) && (args.zip ps).all (fun q => argOk (Rules.full m) g q.1 q.2) &&
      (!(Rules.full m).roots || rootsOk g (args.zip ps))) = true) : ArgsOk m g args ps := by
  simp only [Bool.and_eq_true, decide_eq_true_eq, List.all_eq_true, Rules.full, Bool.not_true, Bool.false_or] at hc
  obtain ⟨⟨h1, h2⟩, h3⟩ := hc
  have hz : ∀ (i v : Nat) (k : PK), args[i]? = some v → ps[i]? = some k → (args.zip ps)[i]? = some (v, k) := by
    intro i v k e1 e2
    exact List.getElem?_zip_eq_some.mpr ⟨e1, e2⟩
  refine ⟨h1, ?_, ?_⟩
  · intro i v k e1 e2
    exact h2 (v, k) (List.mem_of_getElem? (hz i v k e1 e2))
  · intro i j v w k l e1 e2 e3 e4 hij hk
    have a1 := hz i v k e1 e2
    have a2 := hz j w l e3 e4
    have l1 := (List.getElem?_eq_some_iff.mp a1).1
    have l2 := (List.getElem?_eq_some_iff.mp a2).1
    simp only [rootsOk, List.all_eq_true, List.mem_range] at h3
    have := h3 i l1 j l2
    simp only [a1, a2] at this
    simp only [Bool.or_eq_true, beq_iff_eq, Bool.and_eq_true, bne_iff_ne, ne_eq] at this
    rcases this with (h | h) | h
    · exact absurd h hij
    · exact absurd h.1 hk
    · exact h

theorem ArgsOk.lv {args : List Nat} {ps : List PK} (A : ArgsOk m g args ps) {i v : Nat} {k : PK} (h1 : args[i]? = some v)
    (h2 : ps[i]? = some k) : (g v).lv = true ∧ (k ≠ .shr → (g v).ex = true ∧ (g (rootOf g v)).lv = true) ∧
      (k = .own ∨ k = .succ → g v = .own) := by
  have := A.ok i v k h1 h2
  cases k with
  | own => simp only [argOk, decide_eq_true_eq] at this
           simp [this, K.lv, K.ex, rootOf, K.path]
  | succ => simp only [argOk, decide_eq_true_eq] at this
            simp [this, K.lv, K.ex, rootOf, K.path]
  | excl => have q := mutOk_ex this; exact ⟨ex_lv q.1, fun _ => q, by simp⟩
  | shr => exact ⟨this, by simp, by simp⟩

theorem ArgsOk.ex {args : List Nat} {ps : List PK} (A : ArgsOk m g args ps) :
    ∀ e, Args args ps e → (resolve g bs e).ex = true := by
  rintro e ⟨i, k, h1, h2, h3⟩
  obtain ⟨q1, q2, _⟩ := A.lv h1 h2
  rw [res_lv q1]; exact (q2 h3).1

theorem initK_facts {k : PK} : (initK k).path = [] ∧ ((initK k).ex = true → k ≠ .shr) ∧
    (initK k = .own → k = .own ∨ k = .succ) := by
  cases k <;> simp [initK, K.path, K.ex]

theorem init_lv {fn : Fn} {x} (hx : (init fn x).lv = true) : ∃ k, fn.params[x]? = some k ∧ init fn x = initK k := by
  unfold init at hx ⊢
  cases e : fn.params[x]? with
  | none => rw [e] at hx; cases hx
  | some k => exact ⟨k, rfl, rfl⟩

theorem init_res (fn : Fn) (bs) : resolve (init fn) bs = init fn := by
  funext x
  unfold resolve
  split
  · rename_i b e
    unfold init at e
    cases q : fn.params[x]? with
    | none => rw [q] at e; cases e
    | some k => rw [q] at e; cases k <;> cases e
  · rfl

theorem callee_init (fn : Fn) {args : List Nat} (A : ArgsOk m g args fn.params)
    (I : Inv np env0 F H0 (resolve g bs) env h) :
    Inv fn.params.length (argv env args) (Touch env h (Args args fn.params)) h (init fn) (argv env args) h := by
  have get : ∀ {x a}, (init fn x).lv = true → argv env args x = some a →
      ∃ v k, args[x]? = some v ∧ fn.params[x]? = some k ∧ init fn x = initK k ∧ env v = some a := by
    intro x a hx ha
    obtain ⟨k, h1, h2⟩ := init_lv hx
    unfold argv at ha
    cases e : args[x]? with
    | none => rw [e] at ha; cases ha
    | some v => rw [e] at ha; exact ⟨v, k, rfl, h1, h2, ha⟩
  have pathnil : ∀ x, (init fn x).path = [] := by
    intro x; unfold init; split
    · exact initK_facts.1
    · rfl
  have lv' : ∀ {v}, (g v).lv = true → (resolve g bs v).lv = true := fun h => by rw [res_lv h]; exact h
  have hdis : ∀ {x y a b v w : Nat} {k l : PK}, args[x]? = some v → fn.params[x]? = some k → args[y]? = some w →
      fn.params[y]? = some l → x ≠ y → k ≠ .shr → env v = some a → env w = some b → Disj h a b := by
    intro x y a b v w k l h1 h2 h3 h4 hne hk ha hb
    obtain ⟨q1, q2, _⟩ := A.lv h1 h2
    obtain ⟨r1, _, _⟩ := A.lv h3 h4
    refine I.disj (lv' q1) (by rw [res_lv q1]; exact (q2 hk).1) (lv' r1) ?_ ha hb
    rw [res_root, res_root]; exact A.roots _ _ _ _ _ _ h1 h2 h3 h4 hne hk
  refine { wf := I.wf, rt := ?_, anc := ?_, al := ?_, sep := ?_, par := ?_, parE := ?_, inF := ?_,
           Fcl := touch_cl I.wf, fr := fun _ _ => ⟨rfl, fun _ => rfl⟩, fresh := fun _ h => .inr h,
           nx := Nat.le_refl _, p0 := fun _ _ => rfl }
  · intro x hx; rw [root_of_nil (pathnil x)]; exact ⟨hx, pathnil x, id⟩
  · intro x q a hx hq; rw [pathnil x] at hq; cases hq
  · intro x a hx ha
    obtain ⟨v, k, h1, h2, h3, h4⟩ := get hx ha
    exact I.al v a (lv' (A.lv h1 h2).1) h4
  · intro x y a b hx hy _ _ hxy ex ha hb
    obtain ⟨v, k, h1, h2, h3, h4⟩ := get hx ha
    obtain ⟨w, l, k1, k2, k3, k4⟩ := get hy hb
    exact hdis h1 h2 k1 k2 hxy (initK_facts.2.1 (h3 ▸ ex)) h4 k4
  · intro x a b f hx ha
    obtain ⟨v, k, h1, h2, h3, h4⟩ := get (by rw [hx]; rfl) ha
    have := (A.lv h1 h2).2.2 (initK_facts.2.2 (h3 ▸ hx))
    exact I.par v a b f (by rw [res_lv (by rw [this]; rfl), this]) h4
  · intro x a b f hx _ ex ha hb e
    obtain ⟨v, k, h1, h2, h3, h4⟩ := get hx ha
    have hk := initK_facts.2.1 (h3 ▸ ex)
    rcases hb with ⟨w, d, ⟨j, l, j1, j2, j3⟩, hd, r⟩ | hb
    · by_cases hj : x = j
      · subst hj; rw [h1] at j1; cases j1
        rw [h4] at hd; cases hd
        exact I.wf.acyc _ _ _ e r
      · exact hdis h1 h2 j1 j2 hj hk h4 hd a (.refl _) (r.trans (.one e))
    · have := (I.wf.lt _ _ _ e).1; omega
  · intro x a hx _ ex ha
    obtain ⟨v, k, h1, h2, h3, h4⟩ := get hx ha
    exact .inl ⟨v, a, ⟨x, k, h1, h2, initK_facts.2.1 (h3 ▸ ex)⟩, h4, .refl _⟩

/-! ## Returning -/

/-- What a callee guarantees of its final (resolved) state and result. -/
structure RetOk (fn : Fn) (gr : St) (env : Nat → Option Nat) (v : RV) : Prop where
  excl : ∀ i : Nat, fn.params[i]? = some PK.excl → gr i = .bor true []
  succ : ∀ i : Nat, fn.params[i]? = some PK.succ → v = .b false → gr i = .own
  val : match fn.rt with
    | .unit => v = .unit
    | .bool => ∃ b, v = .b b
    | .own => ∃ x, gr x = .own ∧ v = .p (env x)
    | .bor i => ∃ x, (gr x).lv = true ∧ i ∈ clo gr x ∧ v = .p (env x)

theorem ret_ok {fn : Fn} {r : RExp} {s : S} (hp : retParams (Rules.full m) g r fn.params = true)
    (hv : retVOk (Rules.full m) g fn.rt r = true) : RetOk fn (resolve g s.bs) s.env (retVal s r) := by
  have hp' : ∀ (i : Nat) (k : PK), fn.params[i]? = some k → retParam (Rules.full m) g r i k = true := by
    intro i k e
    simp only [retParams, List.all_eq_true, List.mem_range] at hp
    have := hp i (List.getElem?_eq_some_iff.mp e).1
    simpa [e] using this
  refine ⟨?_, ?_, ?_⟩
  · intro i e
    have := hp' i _ e
    simp only [retParam, decide_eq_true_eq] at this
    simp [resolve, this]
  · intro i e hb
    have := hp' i _ e
    cases r with
    | unit => simp [retParam] at this
    | ptr x => simp [retParam] at this
    | lit b =>
      cases b with
      | true => simp [retVal] at hb
      | false => simp [retParam, Rules.full] at this; simp [resolve, this]
    | bvar b =>
      simp [retParam, Rules.full] at this
      simp only [retVal, RV.b.injEq] at hb
      simp [resolve, this, hb]
  · cases e : fn.rt with
    | unit => cases r <;> simp_all [retVOk, retVal]
    | bool => cases r <;> simp_all [retVOk, retVal]
    | own =>
      cases r with
      | ptr x => simp [retVOk, e] at hv; exact ⟨x, by simp [resolve, hv], rfl⟩
      | _ => simp [retVOk, e] at hv
    | bor i =>
      cases r with
      | ptr x =>
        simp [retVOk, e, Rules.full] at hv
        exact ⟨x, by rw [res_lv hv.1]; exact hv.1, by rw [res_clo]; exact hv.2, rfl⟩
      | _ => simp [retVOk, e] at hv

/-- The caller's booleans after the call. -/
def bsAfter (d : Dest) (v : RV) (bs : Nat → Bool) : Nat → Bool :=
  match d, v with | .b b, .b v => upd bs b v | _, _ => bs

theorem untieD_le (d : Dest) (v : RV) (x) :
    KLe (resolve (untieD (Rules.full m) d g) (bsAfter d v bs) x) (resolve g bs x) := by
  cases d with
  | none => exact KLe.rfl
  | p x => exact KLe.rfl
  | b b =>
    cases v with
    | b v => exact untie_le b v g x
    | _ =>
      refine (res_le (prune_le _ _)).trans (res_le ?_)
      split
      · exact .inl rfl
      · exact KLe.rfl

/-- Survivors of `untieD` keep their state. -/
theorem untieD_eq (d : Dest) (x) (hx : (untieD (Rules.full m) d g x).lv = true) :
    untieD (Rules.full m) d g x = g x := by
  cases d with
  | none => rfl
  | p x => rfl
  | b b =>
    rcases prune_cases (fun x => if (Rules.full m).untie && g x == .maybe b then K.dead else g x) x with e | ⟨e, _⟩
    · have : untieD (Rules.full m) (.b b) g x = .dead := e
      rw [this] at hx; cases hx
    · have e' : untieD (Rules.full m) (.b b) g x = if (Rules.full m).untie && g x == .maybe b then K.dead else g x := e
      rw [e'] at hx ⊢
      split at hx
      · cases hx
      · rename_i hh; rw [ite_eq_right hh]

theorem untieD_keep (d : Dest) {x} (hl : (g x).lv = true) (hr : (g x).path = [] ∨
    ((g (rootOf g x)).lv = true ∧ (g (rootOf g x)).path = [] ∧ ((g x).ex = true → (g (rootOf g x)).ex = true))) :
    untieD (Rules.full m) d g x = g x := by
  cases d with
  | none => rfl
  | p x => rfl
  | b b =>
    have nm : ∀ y, (g y).lv = true → (if (Rules.full m).untie && g y == .maybe b then K.dead else g y) = g y := by
      intro y hy
      have : g y ≠ .maybe b := by intro e; rw [e] at hy; cases hy
      simp [this]
    show prune (fun x => if (Rules.full m).untie && g x == .maybe b then K.dead else g x) x = g x
    rcases hr with hr | hr
    · rw [prune_keep] <;> rw [nm x hl]; exact hr
    · rw [prune_keep'] <;> rw [nm x hl]
      have : rootOf (fun x => if (Rules.full m).untie && g x == .maybe b then K.dead else g x) x = rootOf g x := by
        simp only [rootOf, nm x hl]
      rw [this, nm _ hr.1]; exact hr

section
variable {args : List Nat} {ps : List PK} {d : Dest}

/-- `argSt` before pruning. -/
def argIn (_m : Bool) (d : Dest) (az : List (Nat × PK)) (g : St) : St := fun x =>
  if az.contains (x, .own) then .dead
  else if az.contains (x, .succ) then succK d
  else if az.any (fun q => q.2 == .excl && hit g q.1 x) then .dead
  else g x

theorem argSt_eq (az : List (Nat × PK)) : argSt (Rules.full m) d az g = prune (argIn m d az g) := by
  funext x; simp only [argSt, Rules.full, Bool.true_and]; rfl

theorem az_mem {x : Nat} {k : PK} : (args.zip ps).contains (x, k) = true ↔ ∃ i : Nat, args[i]? = some x ∧ ps[i]? = some k := by
  rw [List.contains_iff_mem, List.mem_iff_getElem?]
  simp only [List.getElem?_zip_eq_some]

theorem argIn_le (A : ArgsOk m g args ps) (v : RV) (hv : ∀ i : Nat, ps[i]? = some PK.succ → ∃ b, v = .b b) (x) :
    KLe (resolve (argIn m d (args.zip ps) (untieD (Rules.full m) d g)) (bsAfter d v bs) x) (resolve g bs x) := by
  by_cases h1 : (args.zip ps).contains (x, PK.own) = true
  · have e : argIn m d (args.zip ps) (untieD (Rules.full m) d g) x = .dead := by
      unfold argIn; rw [ite_eq_left h1]
    left; simp only [resolve, e]
  · by_cases h2 : (args.zip ps).contains (x, PK.succ) = true
    · obtain ⟨i, e1, e2⟩ := az_mem.mp h2
      have ho := (A.lv e1 e2).2.2 (.inr rfl)
      have hr : resolve g bs x = .own := by simp [resolve, ho]
      rw [hr]
      obtain ⟨b', rfl⟩ := hv i e2
      have e : argIn m d (args.zip ps) (untieD (Rules.full m) d g) x =
          succK d := by
        unfold argIn; rw [ite_eq_right h1, ite_eq_left h2]
      cases d with
      | b b =>
        cases b'
        · right; left; simp [resolve, e, bsAfter, upd, succK]
        · left; simp [resolve, e, bsAfter, upd, succK]
      | _ => left; simp only [resolve, e, succK]
    · by_cases h3 : (args.zip ps).any (fun q => q.2 == PK.excl && hit (untieD (Rules.full m) d g) q.1 x) = true
      · have e : argIn m d (args.zip ps) (untieD (Rules.full m) d g) x = .dead := by
          unfold argIn; rw [ite_eq_right h1, ite_eq_right h2, ite_eq_left h3]
        left; simp only [resolve, e]
      · have e : argIn m d (args.zip ps) (untieD (Rules.full m) d g) x = untieD (Rules.full m) d g x := by
          unfold argIn; rw [ite_eq_right h1, ite_eq_right h2, ite_eq_right h3]
        have := untieD_le (m := m) (g := g) (bs := bs) d v x
        simpa only [resolve, e] using this

/-- What is live after the arguments are passed had this state before. -/
theorem argIn_lv {x} (hx : (argIn m d (args.zip ps) (untieD (Rules.full m) d g) x).lv = true) :
    argIn m d (args.zip ps) (untieD (Rules.full m) d g) x = g x ∧ (g x).lv = true ∧
    untieD (Rules.full m) d g x = g x ∧
    (∀ i : Nat, args[i]? = some x → ps[i]? ≠ some PK.own ∧ ps[i]? ≠ some PK.succ) ∧
    (∀ (i w : Nat), args[i]? = some w → ps[i]? = some PK.excl → hit (untieD (Rules.full m) d g) w x = false) := by
  have hd : ¬ (argIn m d (args.zip ps) (untieD (Rules.full m) d g) x = .dead) := by
    intro e; rw [e] at hx; cases hx
  unfold argIn at hx hd ⊢
  by_cases h1 : (args.zip ps).contains (x, PK.own) = true
  · rw [ite_eq_left h1] at hd; exact absurd rfl hd
  · by_cases h2 : (args.zip ps).contains (x, PK.succ) = true
    · rw [ite_eq_right h1, ite_eq_left h2] at hx
      cases d <;> cases hx
    · by_cases h3 : (args.zip ps).any (fun q => q.2 == PK.excl && hit (untieD (Rules.full m) d g) q.1 x) = true
      · rw [ite_eq_right h1, ite_eq_right h2, ite_eq_left h3] at hd; exact absurd rfl hd
      · rw [ite_eq_right h1, ite_eq_right h2, ite_eq_right h3] at hx ⊢
        have he := untieD_eq d x hx
        refine ⟨he, he ▸ hx, he, ?_, ?_⟩
        · intro i e
          exact ⟨fun k => h1 (az_mem.mpr ⟨i, e, k⟩), fun k => h2 (az_mem.mpr ⟨i, e, k⟩)⟩
        · intro i w e k
          have hw : (w, PK.excl) ∈ args.zip ps := List.contains_iff_mem.mp (az_mem.mpr ⟨i, e, k⟩)
          cases q : hit (untieD (Rules.full m) d g) w x with
          | false => rfl
          | true => exact absurd (List.any_eq_true.mpr ⟨_, hw, by simp [q]⟩) h3

theorem arg_untie (A : ArgsOk m g args ps) (I : Inv np env0 F H0 (resolve g bs) env h) {i e : Nat} {k : PK}
    (h1 : args[i]? = some e) (h2 : ps[i]? = some k) (hk : k ≠ .shr) :
    untieD (Rules.full m) d g e = g e := by
  obtain ⟨q1, q2, _⟩ := A.lv h1 h2
  exact untieD_keep d q1 (.inr (rt_un I q1 (q2 hk).2))

/-- After the call, before the result is assigned. -/
theorem call_back {fn : Fn} (A : ArgsOk m g args fn.params) (hs : fnShape fn = true)
    (I : Inv np env0 F H0 (resolve g bs) env h) {gr : St} {envr : Nat → Option Nat} {h' : Heap} {v : RV}
    (J : Inv fn.params.length (argv env args) (Touch env h (Args args fn.params)) h gr envr h')
    (R : RetOk fn gr envr v) :
    Inv np env0 F H0 (resolve (argSt (Rules.full m) d (args.zip fn.params) (untieD (Rules.full m) d g))
      (bsAfter d v bs)) env h' ∧
    (∀ x, KLe (resolve (argSt (Rules.full m) d (args.zip fn.params) (untieD (Rules.full m) d g))
      (bsAfter d v bs) x) (resolve g bs x)) ∧
    (∀ e r, Args args fn.params e → rootOf g e = r →
      (resolve (argSt (Rules.full m) d (args.zip fn.params) (untieD (Rules.full m) d g)) (bsAfter d v bs) r).lv = true →
      ∃ (i : Nat) (k : PK), args[i]? = some e ∧ fn.params[i]? = some k ∧ (gr i).lv = true ∧ (gr i).path = [] ∧
        (gr i).ex = true ∧ (k = .excl ∧ gr i = .bor true [] ∨ k = .succ ∧ fn.rt = .bool)) := by
  have hsucc : ∀ i : Nat, fn.params[i]? = some PK.succ → fn.rt = .bool := by
    intro i e
    have hm : PK.succ ∈ fn.params := List.mem_of_getElem? e
    simp only [fnShape, Bool.and_eq_true, Bool.or_eq_true, Bool.not_eq_true', beq_iff_eq] at hs
    rcases hs.1 with q | q
    · have : fn.params.contains PK.succ = true := List.contains_iff_mem.mpr hm
      rw [this] at q; cases q
    · exact q
  have hvb : ∀ i : Nat, fn.params[i]? = some PK.succ → ∃ b, v = .b b := by
    intro i e; have := R.val; rw [hsucc i e] at this; exact this
  generalize hg1 : argSt (Rules.full m) d (args.zip fn.params) (untieD (Rules.full m) d g) = g1
  have g1e : g1 = prune (argIn m d (args.zip fn.params) (untieD (Rules.full m) d g)) := by
    rw [← hg1, argSt_eq]
  have hle : ∀ x, KLe (resolve g1 (bsAfter d v bs) x) (resolve g bs x) := by
    intro x; rw [g1e]
    exact (res_le (prune_le _ _)).trans (argIn_le A v hvb x)
  -- an exclusive argument keeps its state
  have exk : ∀ (e i : Nat), args[i]? = some e → fn.params[i]? = some PK.excl → g1 e = g e := by
    intro e i h1 h2
    have hk : PK.excl ≠ PK.shr := by simp
    obtain ⟨q1, q2, _⟩ := A.lv h1 h2
    have hru := rt_un I q1 (q2 hk).2
    have keep : ∀ z ∈ clo g e, rootOf g z = rootOf g e → untieD (Rules.full m) d g z = g z →
        argIn m d (args.zip fn.params) (untieD (Rules.full m) d g) z = g z := by
      intro z hz hzr hzu
      have n1 : ∀ k', (k' = PK.own ∨ k' = PK.succ) → ¬ (args.zip fn.params).contains (z, k') = true := by
        intro k' hk' q
        obtain ⟨j, j1, j2⟩ := az_mem.mp q
        by_cases hij : i = j
        · subst hij; rw [h2] at j2; cases j2; simp at hk'
        · exact A.roots _ _ _ _ _ _ h1 h2 j1 j2 hij hk hzr.symm
      have n2 : ¬ (args.zip fn.params).any
          (fun q => q.2 == PK.excl && hit (untieD (Rules.full m) d g) q.1 z) = true := by
        intro q
        obtain ⟨⟨w, k'⟩, w1, w2⟩ := List.any_eq_true.mp q
        simp only [Bool.and_eq_true, beq_iff_eq] at w2
        obtain ⟨rfl, w2⟩ := w2
        obtain ⟨j, j1, j2⟩ := az_mem.mp (List.contains_iff_mem.mpr w1)
        have hwu := arg_untie (d := d) A I j1 j2 hk
        have hrw : rootOf (untieD (Rules.full m) d g) w = rootOf g w := by simp [rootOf, hwu]
        have hrz : rootOf (untieD (Rules.full m) d g) z = rootOf g z := by simp [rootOf, hzu]
        have hcw : clo (untieD (Rules.full m) d g) w = clo g w := by simp [clo, hwu]
        simp only [hit, hrw, hrz, hcw, Bool.and_eq_true, beq_iff_eq, Bool.not_eq_true',
          ← Bool.not_eq_true, List.contains_iff_mem] at w2
        by_cases hij : i = j
        · subst hij; rw [h1] at j1; cases j1; exact w2.2 hz
        · exact A.roots _ _ _ _ _ _ h1 h2 j1 j2 hij hk (hzr.symm.trans w2.1)
      unfold argIn
      rw [ite_eq_right (n1 _ (.inl rfl)), ite_eq_right (n1 _ (.inr rfl)), ite_eq_right n2, hzu]
    have ke := keep e (by simp [clo]) rfl (arg_untie A I h1 h2 hk)
    have kr := keep (rootOf g e) (root_clo g e) (root_of_nil hru.2.1) (untieD_keep d hru.1 (.inl hru.2.1))
    have hro : rootOf (argIn m d (args.zip fn.params) (untieD (Rules.full m) d g)) e = rootOf g e := by
      simp only [rootOf, ke]
    rw [g1e, prune_keep', ke]
    rw [hro, ke, kr]; exact hru
  -- an argument whose own state or root survives
  have surv : ∀ e r, Args args fn.params e → rootOf g e = r → (resolve g1 (bsAfter d v bs) r).lv = true →
      (resolve g1 (bsAfter d v bs) e).lv = true ∧
      ∃ (i : Nat) (k : PK), args[i]? = some e ∧ fn.params[i]? = some k ∧ (gr i).lv = true ∧ (gr i).path = [] ∧
        (gr i).ex = true ∧ (k = .excl ∧ gr i = .bor true [] ∨ k = .succ ∧ fn.rt = .bool) := by
    rintro e r ⟨i, k, h1, h2, hk⟩ hr hl
    obtain ⟨q1, q2, q3⟩ := A.lv h1 h2
    by_cases hown : k = .own ∨ k = .succ
    · have ho := q3 hown
      have : rootOf g e = e := root_of_nil (by rw [ho]; rfl)
      have hre : r = e := hr.symm.trans this
      subst hre
      refine ⟨hl, i, k, h1, h2, ?_⟩
      have h5 : g1 r = argIn m d (args.zip fn.params) (untieD (Rules.full m) d g) r := by
        have hp0 : (argIn m d (args.zip fn.params) (untieD (Rules.full m) d g) r).path = [] := by
          have hu : untieD (Rules.full m) d g r = .own := by rw [arg_untie A I h1 h2 hk, ho]
          unfold argIn; rw [hu]
          split
          · rfl
          · split
            · cases d <;> rfl
            · split <;> rfl
        rw [g1e, prune_keep hp0]
      rcases hown with rfl | rfl
      · have : g1 r = .dead := by
          rw [h5]; unfold argIn; rw [ite_eq_left (az_mem.mpr ⟨i, h1, h2⟩)]
        simp [resolve, this, K.lv] at hl
      · have hn : ¬ (args.zip fn.params).contains (r, PK.own) = true := by
          intro q
          obtain ⟨j, j1, j2⟩ := az_mem.mp q
          by_cases hij : i = j
          · subst hij; rw [h2] at j2; cases j2
          · exact A.roots _ _ _ _ _ _ h1 h2 j1 j2 hij hk rfl
        have h6 : g1 r = succK d := by
          rw [h5]; unfold argIn; rw [ite_eq_right hn, ite_eq_left (az_mem.mpr ⟨i, h1, h2⟩)]
        obtain ⟨b, hb⟩ := hvb i h2
        cases d with
        | none => simp [resolve, h6, succK, K.lv] at hl
        | p _ => simp [resolve, h6, succK, K.lv] at hl
        | b b' =>
          subst hb
          have hbf : b = false := by
            cases b
            · rfl
            · simp [resolve, h6, succK, bsAfter, upd, K.lv] at hl
          have := R.succ i h2 (by rw [hbf])
          rw [this]
          exact ⟨rfl, rfl, rfl, .inr ⟨rfl, hsucc i h2⟩⟩
    · have hke : k = .excl := by cases k <;> simp_all
      subst hke
      refine ⟨?_, i, _, h1, h2, ?_⟩
      · have := exk e i h1 h2
        rw [res_lv (by rw [this]; exact q1), this]; exact q1
      · have := R.excl i h2
        rw [this]; exact ⟨rfl, rfl, rfl, .inl ⟨rfl, rfl⟩⟩
  refine ⟨?_, hle, fun e r h1 h2 h3 => (surv e r h1 h2 h3).2⟩
  have self : ∀ e, Args args fn.params e → (resolve g1 (bsAfter d v bs) e).lv = true →
      ∃ i : Nat, args[i]? = some e ∧ (gr i).lv = true ∧ (gr i).path = [] ∧ (gr i).ex = true := by
    intro e he hl
    have hl0 := hl
    obtain ⟨hk1, _, _, hk4, _⟩ := (hle e).facts hl
    -- the root of a survivor survives
    have hr : (resolve g1 (bsAfter d v bs) (rootOf g e)).lv = true := by
      have t := (rt_prune (argIn m d (args.zip fn.params) (untieD (Rules.full m) d g)) (bsAfter d v bs)) e
        (by rw [← g1e]; exact hl)
      rw [← g1e, res_root] at t
      by_cases hp : (g e).path = []
      · rw [root_of_nil hp]; exact hl
      · have hp1 : (g1 e).path ≠ [] := fun q => hp (by
          rw [← res_path g bs]; exact hk4.mp (by rw [res_path]; exact q))
        have hl1 : (g1 e).lv = true := by rw [← res_bor (bs := bsAfter d v bs) hp1]; exact hl
        have : g1 e = g e := by
          rw [g1e] at hl1 ⊢
          rcases prune_cases (argIn m d (args.zip fn.params) (untieD (Rules.full m) d g)) e with q | ⟨q, _⟩
          · rw [q] at hl1; cases hl1
          · rw [q] at hl1 ⊢; exact (argIn_lv hl1).1
        have hro : rootOf g1 e = rootOf g e := by simp [rootOf, this]
        rw [← hro]; exact t.1
    obtain ⟨_, i, k, i1, i2, i3, i4, i5, _⟩ := surv e _ he rfl hr
    exact ⟨i, i1, i3, i4, i5⟩
  have hget : ∀ (i e a : Nat), args[i]? = some e → env e = some a → envr i = some a := by
    intro i e a i1 ha
    have : i < fn.params.length := by rw [← A.len]; exact (List.getElem?_eq_some_iff.mp i1).1
    rw [J.p0 i this]; simp [argv, i1, ha]
  refine I.touch (Args args fn.params) A.ex J.wf J.nx (fun n hn => J.fr n hn) J.Fcl hle (by rw [g1e]; exact rt_prune _ _)
    ?_ ?_ ?_ ?_
  · intro y e he hy
    rw [res_root, res_root, res_clo]
    refine Classical.byContradiction fun hn => ?_
    simp only [not_or, Classical.not_not] at hn
    obtain ⟨hn1, hn2⟩ := hn
    obtain ⟨_, _, _, hk4, _⟩ := (hle y).facts hy
    have hye : y ≠ e := fun q => hn1 (by simp [clo, q])
    obtain ⟨i, k, h1, h2, hk⟩ := he
    obtain ⟨q1, q2, q3⟩ := A.lv h1 h2
    by_cases hown : k = .own ∨ k = .succ
    · have ho := q3 hown
      have hre : rootOf g e = e := root_of_nil (by rw [ho]; rfl)
      rw [hre] at hn2
      have hp : (g y).path ≠ [] := fun q => hye ((root_of_nil q).symm.trans hn2)
      have hp1 : (g1 y).path ≠ [] := fun q => hp (by
          rw [← res_path g bs]; exact hk4.mp (by rw [res_path]; exact q))
      have hl1 : (g1 y).lv = true := by rw [← res_bor (bs := bsAfter d v bs) hp1]; exact hy
      rw [g1e] at hl1 hp1
      rcases prune_cases (argIn m d (args.zip fn.params) (untieD (Rules.full m) d g)) y with q | ⟨q, q'⟩
      · rw [q] at hl1; cases hl1
      · rw [q] at hl1 hp1
        have hy1 := (argIn_lv hl1).1
        have hro : rootOf (argIn m d (args.zip fn.params) (untieD (Rules.full m) d g)) y = e := by
          rw [← hn2]; simp only [rootOf, hy1]
        have := (argIn_lv (hro ▸ (q' hp1).1)).2.2.2.1 i h1
        rcases hown with rfl | rfl
        · exact this.1 h2
        · exact this.2 h2
    · have hke : k = .excl := by cases k <;> simp_all
      subst hke
      have hl1 : (argIn m d (args.zip fn.params) (untieD (Rules.full m) d g) y).lv = true := by
        rcases prune_cases (argIn m d (args.zip fn.params) (untieD (Rules.full m) d g)) y with q | ⟨q, _⟩
        · rw [g1e] at hy; simp [resolve, q, K.lv] at hy
        · -- `y` is live only if its unresolved state is: a resolved `maybe` is a root equal to itself
          have hyl : (g1 y).lv = true := by
            by_cases hp : (g1 y).path = []
            · have hp0 : (g y).path = [] := by rw [← res_path g bs]; exact hk4.mp (by rw [res_path]; exact hp)
              have hyr : rootOf g y = y := root_of_nil hp0
              exact absurd (by rw [← hyr, hn2]; exact root_clo g e) hn1
            · rw [← res_bor (bs := bsAfter d v bs) hp]; exact hy
          rw [g1e, q] at hyl; exact hyl
      obtain ⟨_, _, hyu, _, hh⟩ := argIn_lv hl1
      have := hh i e h1 h2
      have heu := arg_untie (d := d) A I h1 h2 hk
      simp only [hit, rootOf, clo, hyu, heu] at this
      simp only [rootOf] at hn2
      simp only [clo] at hn1
      simp at this hn1
      exact hn1.2 (this (by simpa using hn2) hn1.1)
  · intro e he
    obtain ⟨i, k, h1, h2, hk⟩ := he
    by_cases hown : k = .own ∨ k = .succ
    · right; have := (A.lv h1 h2).2.2 hown; simp [resolve, this]
    · have hke : k = .excl := by cases k <;> simp_all
      subst hke
      left
      have := exk e i h1 h2
      have ql := (A.lv h1 h2).1
      rw [res_lv (by rw [this]; exact ql), this]; exact ql
  · intro e a he hl ha
    obtain ⟨i, i1, i2, i3, i4⟩ := self e he hl
    have := hget i e a i1 ha
    exact ⟨J.al i a i2 this, fun b f hb => J.parE i a b f i2 i3 i4 this hb⟩
  · intro e1 e2 a b h1 h2 hne l1 l2 ha hb
    obtain ⟨i, i1, i2, i3, i4⟩ := self e1 h1 l1
    obtain ⟨j, j1, j2, j3, j4⟩ := self e2 h2 l2
    have hij : i ≠ j := by rintro rfl; rw [i1] at j1; cases j1; exact hne rfl
    exact J.sep i j a b i2 j2 i3 j3 hij i4 (hget i _ _ i1 ha) (hget j _ _ j1 hb)
end

theorem call_ok {P : Prog} {fn : Fn} {f : Nat} {args : List Nat} {d : Dest} {mo : Mode} {g' : St}
    (hf : P[f]? = some fn) (hs : fnShape fn = true)
    (hc : callK (Rules.full m) P np d mo f args g = some g')
    (I : Inv np env0 F H0 (resolve g bs) env h) {gr : St} {envr : Nat → Option Nat} {h' : Heap} {v : RV}
    (J : Inv fn.params.length (argv env args) (Touch env h (Args args fn.params)) h gr envr h')
    (R : RetOk fn gr envr v) (orc : Nat → Bool) :
    Inv np env0 F H0 (resolve g' (assign d v ⟨h', env, bs, orc⟩).bs) (assign d v ⟨h', env, bs, orc⟩).env
      (assign d v ⟨h', env, bs, orc⟩).h := by
  simp only [callK, hf] at hc
  obtain ⟨hcond, hc⟩ := grd_some.mp hc
  have A := argsOk_of hcond
  obtain ⟨B, hle, surv⟩ := call_back (d := d) A hs I J R
  have eB : bsAfter d v bs = (assign d v ⟨h', env, bs, orc⟩).bs := by
    cases d <;> cases v <;> rfl
  have eH : (assign d v ⟨h', env, bs, orc⟩).h = h' := by
    cases d <;> cases v <;> rfl
  rw [eH, ← eB]
  generalize argSt (Rules.full m) d (args.zip fn.params) (untieD (Rules.full m) d g) = g1 at *
  cases d with
  | none =>
    simp only [assignK, Option.some.injEq] at hc; subst hc
    exact B
  | b b =>
    have : (assign (.b b) v ⟨h', env, bs, orc⟩).env = env := by cases v <;> rfl
    rw [this]
    cases hrt : fn.rt <;> rw [hrt] at hc <;> simp only [assignK, reduceCtorEq, Option.some.injEq] at hc
    subst hc; exact B
  | p x =>
    have hbs : bsAfter (.p x) v bs = bs := by cases v <;> rfl
    rw [hbs] at B surv hle ⊢
    have RV := R.val
    cases hrt : fn.rt with
    | unit => rw [hrt] at hc; simp [assignK] at hc
    | bool => rw [hrt] at hc; simp [assignK] at hc
    | own =>
      rw [hrt] at hc RV
      obtain ⟨z, hz, rfl⟩ := RV
      simp only [assignK] at hc
      obtain ⟨hx, hc⟩ := grd_some.mp hc
      cases hc
      refine B.assignOwn hx _ fun a ha => ?_
      have hzl : (gr z).lv = true := by rw [hz]; rfl
      have aT : Touch env h (Args args fn.params) a := J.inF z a hzl (by rw [hz]; rfl) (by rw [hz]; rfl) ha
      refine ⟨J.al z a hzl ha, touch_F I A.ex a aT, fun b f => J.par z a b f hz ha, ?_⟩
      intro y b hy hp hb c p q
      have tr : ∀ {n c}, Reach h' n c → Touch env h (Args args fn.params) n →
          Touch env h (Args args fn.params) c := by
        intro n c r
        induction r with
        | refl => exact id
        | step e _ ih => exact fun t => ih (J.Fcl _ _ _ t e)
      -- y was live before the call
      have hy0 := hy
      obtain ⟨y1, _, _, y4, _⟩ := (hle y).facts hy
      rcases I.entryR A.ex (fun n hn => J.fr n hn) (r := y) y1
        (y4.mp (by rw [res_path]; exact hp)) hb q with ⟨h1, _⟩ | ⟨e, b', h1, h2, h3, h4, h5⟩
      · exact h1 (tr p aT)
      · rw [res_root] at h5
        obtain ⟨i, k, i1, i2, i3, i4, i5, i6⟩ := surv e y h1 h5 hy0
        have hiz : z ≠ i := by
          rintro rfl
          rcases i6 with ⟨_, i6⟩ | ⟨_, i6⟩
          · rw [hz] at i6; cases i6
          · rw [hrt] at i6; cases i6
        have hel : envr i = some b' := by
          have : i < fn.params.length := by rw [← A.len]; exact (List.getElem?_eq_some_iff.mp i1).1
          rw [J.p0 i this]; simp [argv, i1, h2]
        exact J.sep z i a b' hzl i3 (by rw [hz]; rfl) i4 hiz (by rw [hz]; rfl) ha hel c p h4
    | bor i =>
      rw [hrt] at hc RV
      obtain ⟨z, hz, hi, rfl⟩ := RV
      simp only [assignK] at hc
      cases hv : args[i]? with
      | none => rw [hv] at hc; cases hc
      | some w =>
        rw [hv] at hc
        refine borrow_ok _ ⟨rfl, rfl, rfl⟩ B _ hc ?_
        intro a ha
        obtain ⟨b, hb, r⟩ := J.anc' hz hi ha
        have : i < fn.params.length := by rw [← A.len]; exact (List.getElem?_eq_some_iff.mp hv).1
        rw [J.p0 i this] at hb
        exact ⟨b, by simpa [argv, hv] using hb, r⟩

/-! ## The main theorem -/

/-- What a run from a checked state can do. -/
def Good (fn : Fn) (env0 : Nat → Option Nat) (F : Nat → Prop) (H0 : Heap) (res : Option St) : Out → Prop
  | .ok s => ∃ g', res = some g' ∧ Inv fn.params.length env0 F H0 (resolve g' s.bs) s.env s.h
  | .ret s v => ∃ gr, Inv fn.params.length env0 F H0 gr s.env s.h ∧ RetOk fn gr s.env v
  | .trap => True
  | .oof => True
  | .err => False

theorem Good.join_l {fn env0 F H0} {g1 g2 : Option St} : ∀ {o}, Good fn env0 F H0 g1 o → Good fn env0 F H0 (joinR g1 g2) o
  | .ok s, ⟨g', e, I⟩ => by
    subst e
    cases g2 with
    | none => exact ⟨_, rfl, I⟩
    | some g2 => exact ⟨_, rfl, SafeC.Core.join_l _ _ I⟩
  | .ret _ _, h => h
  | .trap, _ => trivial
  | .oof, _ => trivial

theorem Good.join_r {fn env0 F H0} {g1 g2 : Option St} : ∀ {o}, Good fn env0 F H0 g2 o → Good fn env0 F H0 (joinR g1 g2) o
  | .ok s, ⟨g', e, I⟩ => by
    subst e
    cases g1 with
    | none => exact ⟨_, rfl, I⟩
    | some g1 => exact ⟨_, rfl, SafeC.Core.join_r _ _ I⟩
  | .ret _ _, h => h
  | .trap, _ => trivial
  | .oof, _ => trivial

theorem exec_good (P : Prog) (hP : checkProg (Rules.full m) P = true) : ∀ (n : Nat) (c : Cmd) (fn : Fn)
    (g : St) (res : Option St) (s : S) (env0 : Nat → Option Nat) (F : Nat → Prop) (H0 : Heap),
    chk (Rules.full m) P fn c g = some res →
    Inv fn.params.length env0 F H0 (resolve g s.bs) s.env s.h →
    Good fn env0 F H0 res (exec P n c s) := by
  intro n
  induction n using Nat.strongRecOn with
  | _ n IH =>
  intro c fn g res s env0 F H0 hc I
  cases n with
  | zero => trivial
  | succ n =>
  have der : ∀ {v : Nat} {k : Nat → Out}, (g v).lv = true →
      (∀ a, s.env v = some a → Good fn env0 F H0 res (k a)) → Good fn env0 F H0 res (deref s v k) := by
    intro v k hl hk
    unfold deref
    cases e : s.env v with
    | none => trivial
    | some a =>
      have : s.h.alive a = true := I.al v a (by rw [res_lv hl]; exact hl) e
      simp only [this, ite_true]; exact hk a e
  cases c with
  | skip => cases hc; exact ⟨g, rfl, I⟩
  | new x =>
    obtain ⟨hx, hc⟩ := grd_some.mp hc; cases hc
    exact ⟨_, rfl, alloc_ok I hx⟩
  | nil x =>
    obtain ⟨hx, hc⟩ := grd_some.mp hc; cases hc
    exact ⟨_, rfl, I.assignOwn hx none (fun a e => by cases e)⟩
  | fin x =>
    obtain ⟨hx, hc⟩ := grd_some.mp hc; cases hc
    exact ⟨_, rfl, (I.toDrop x).setGone (pOk_le hx) (drop_gone _ _ _) none⟩
  | borrow x mo v =>
    simp only [chk, Option.map_eq_some_iff] at hc
    obtain ⟨g', hb, rfl⟩ := hc
    exact ⟨_, rfl, borrow_ok _ ⟨rfl, rfl, rfl⟩ I _ hb fun a e => ⟨a, e, .refl _⟩⟩
  | step x mo v f =>
    simp only [chk, Option.map_eq_some_iff] at hc
    obtain ⟨g', hb, rfl⟩ := hc
    have hl : (g v).lv = true := by
      simp only [borrowK] at hb
      have := (grd_some.mp hb).1
      simp only [Bool.and_eq_true] at this
      exact this.1.1.2
    refine der hl fun a ha => ?_
    exact ⟨_, rfl, borrow_ok _ ⟨rfl, rfl, rfl⟩ I _ hb fun c e => ⟨a, ha, .one e⟩⟩
  | read b v =>
    obtain ⟨hl, hc⟩ := grd_some.mp hc; cases hc
    exact der hl fun a _ => ⟨_, rfl, untie_ok I b _⟩
  | test b v =>
    obtain ⟨hl, hc⟩ := grd_some.mp hc; cases hc
    exact ⟨_, rfl, untie_ok I b _⟩
  | havoc b => cases hc; exact ⟨_, rfl, untie_ok I b _⟩
  | setb b v => cases hc; exact ⟨_, rfl, untie_ok I b _⟩
  | store v f o =>
    obtain ⟨hl, hc⟩ := grd_some.mp hc; cases hc
    simp only [Bool.and_eq_true, decide_eq_true_eq] at hl
    obtain ⟨⟨h1, h2⟩, h3⟩ := hl
    refine der (ex_lv (mutOk_ex h2).1) fun a ha => ?_
    exact ⟨_, rfl, store_ok I h1 h2 (by simpa [Rules.full] using h3) ha⟩
  | take x v f =>
    obtain ⟨hl, hc⟩ := grd_some.mp hc; cases hc
    simp only [Bool.and_eq_true] at hl
    refine der (ex_lv (mutOk_ex hl.2).1) fun a ha => ?_
    exact ⟨_, rfl, take_ok I hl.1 hl.2 ha⟩
  | mv x o =>
    obtain ⟨hl, hc⟩ := grd_some.mp hc; cases hc
    simp only [Bool.and_eq_true, decide_eq_true_eq] at hl
    obtain ⟨hx, ho⟩ := hl
    have hG : resolve g s.bs o = .own := by simp [resolve, ho]
    refine ⟨_, rfl, (move_ok I o).assignOwn hx _ fun a e => ?_⟩
    have hol : (resolve g s.bs o).lv = true := by rw [hG]; rfl
    refine ⟨I.al o a hol e, I.inF o a hol (by rw [hG]; rfl) (by rw [hG]; rfl) e,
      fun b f => I.par o a b f hG e, fun y b hy hp hb => ?_⟩
    have hro : rootOf g o = o := root_of_nil (by rw [ho]; rfl)
    obtain ⟨y1, _, _, y4, _⟩ := (res_le (bs := s.bs) (moveOut_le (m := m) o g y)).facts hy
    have hne : o ≠ y := by rintro rfl; exact moveOut_hd hy hro
    exact I.sep o y a b hol y1 (by rw [hG]; rfl) (y4.mp (by rw [res_path]; exact hp)) hne (by rw [hG]; rfl) e hb
  | free o =>
    obtain ⟨ho, hc⟩ := grd_some.mp hc; cases hc
    simp only [decide_eq_true_eq] at ho
    simp only [exec]
    cases e : s.env o with
    | none => exact ⟨_, rfl, move_ok I o⟩
    | some a =>
      have : s.h.alive a = true := I.al o a (by simp [resolve, ho, K.lv]) e
      simp only [this, ite_true]
      exact ⟨_, rfl, free_ok I ho e⟩
  | seq c d =>
    simp only [chk] at hc
    simp only [exec]
    cases h1 : chk (Rules.full m) P fn c g with
    | none => simp [h1] at hc
    | some r =>
      have G := IH n (by omega) c fn g r s env0 F H0 h1 I
      cases r with
      | none =>
        simp only [h1, Option.some.injEq] at hc; subst hc
        cases e : exec P n c s with
        | ok s' => rw [e] at G; obtain ⟨_, q, _⟩ := G; cases q
        | err => rw [e] at G; exact G.elim
        | ret s' v => rw [e] at G; exact G
        | trap => trivial
        | oof => trivial
      | some g' =>
        simp only [h1] at hc
        cases e : exec P n c s with
        | ok s' =>
          rw [e] at G; obtain ⟨_, q, J⟩ := G; cases q
          exact IH n (by omega) d fn g' res s' env0 F H0 hc J
        | err => rw [e] at G; exact G.elim
        | ret s' v => rw [e] at G; exact G
        | trap => trivial
        | oof => trivial
  | ite b c d =>
    simp only [chk] at hc
    simp only [exec]
    cases h1 : chk (Rules.full m) P fn c (assume b true g) with
    | none => simp [h1] at hc
    | some r1 =>
      cases h2 : chk (Rules.full m) P fn d (assume b false g) with
      | none => simp [h1, h2] at hc
      | some r2 =>
        simp only [h1, h2, Option.some.injEq] at hc; subst hc
        cases hb : s.bs b with
        | true =>
          simp only [ite_true]
          exact (IH n (by omega) c fn _ r1 s env0 F H0 h1 (by rw [res_assume hb]; exact I)).join_l
        | false =>
          simp only [Bool.false_eq_true, ite_false]
          exact (IH n (by omega) d fn _ r2 s env0 F H0 h2 (by rw [res_assume hb]; exact I)).join_r
  | «while» b c =>
    simp only [chk] at hc
    split at hc
    · cases hc
    · rename_i g0 _
      split at hc
      · cases hc
      · rename_i r hr
        obtain ⟨hl, hc⟩ := grd_some.mp hc; cases hc
        simp only [Bool.and_eq_true] at hl
        have loop : ∀ k, k ≤ n → ∀ s : S,
            Inv fn.params.length env0 F H0 (resolve (trunc fn.nlocals g0) s.bs) s.env s.h →
            Good fn env0 F H0 (some (assume b false (trunc fn.nlocals g0))) (exec P (k + 1) (.while b c) s) := by
          intro k
          induction k with
          | zero =>
            intro _ s I
            simp only [exec]
            cases hb : s.bs b with
            | true => trivial
            | false => exact ⟨_, rfl, by rw [res_assume hb]; exact I⟩
          | succ k ih =>
            intro hk s I
            rw [exec]
            cases hb : s.bs b with
            | false => exact ⟨_, rfl, by rw [res_assume hb]; exact I⟩
            | true =>
              simp only [ite_true]
              have G := IH (k + 1) (by omega) c fn _ r s env0 F H0 hr (by rw [res_assume hb]; exact I)
              cases e : exec P (k + 1) c s with
              | ok s' =>
                rw [e] at G
                obtain ⟨g', rfl, J⟩ := G
                exact ih (by omega) s' (stLe_ok hl.2 J)
              | err => rw [e] at G; exact G.elim
              | ret s' v => rw [e] at G; exact G
              | trap => trivial
              | oof => trivial
        exact loop n (Nat.le_refl _) s (stLe_ok hl.1 I)
  | call d mo f args =>
    simp only [chk, Option.map_eq_some_iff] at hc
    obtain ⟨g', hb, rfl⟩ := hc
    simp only [exec]
    cases hf : P[f]? with
    | none => simp [callK, hf] at hb
    | some fn' =>
      have hok : chkFn (Rules.full m) P fn' = true := by
        simp only [checkProg, List.all_eq_true] at hP
        exact hP fn' (List.mem_of_getElem? hf)
      simp only [chkFn, Bool.and_eq_true] at hok
      have hbody : chk (Rules.full m) P fn' fn'.body (init fn') = some none := by
        have := hok.2
        split at this
        · assumption
        · cases this
      have A : ArgsOk m g args fn'.params := by
        have hb' := hb
        simp only [callK, hf] at hb'
        exact argsOk_of (grd_some.mp hb').1
      have G := IH n (by omega) fn'.body fn' (init fn') none ⟨s.h, argv s.env args, fun _ => false, s.orc⟩
        (argv s.env args) (Touch s.env s.h (Args args fn'.params)) s.h hbody
        (by simp only [init_res]; exact callee_init fn' A I)
      simp only
      cases e : exec P n fn'.body ⟨s.h, argv s.env args, fun _ => false, s.orc⟩ with
      | ok s' => rw [e] at G; obtain ⟨_, q, _⟩ := G; cases q
      | err => rw [e] at G; exact G.elim
      | trap => trivial
      | oof => trivial
      | ret s' v =>
        rw [e] at G
        obtain ⟨gr, J, R⟩ := G
        exact ⟨g', rfl, call_ok hf hok.1 hb I J R s'.orc⟩
  | ret r =>
    obtain ⟨hl, hc⟩ := grd_some.mp hc; cases hc
    simp only [Bool.and_eq_true] at hl
    exact ⟨_, I, ret_ok hl.1 hl.2⟩

/-- Nothing allocated, no locals. -/
theorem inv_empty (env0 : Nat → Option Nat) (bs) :
    Inv 0 env0 (fun _ => True) .empty (resolve (fun _ => .dead) bs) (fun _ => none) .empty := by
  have hd : ∀ x, (resolve (fun _ => .dead) bs x) = .dead := fun _ => rfl
  refine { wf := ?_, rt := ?_, anc := ?_, al := ?_, sep := ?_, par := ?_, parE := ?_, inF := ?_, Fcl := ?_,
           fr := ?_, fresh := ?_, nx := ?_, p0 := ?_ } <;>
    simp_all [Rt, K.lv, Heap.empty]
  constructor <;> simp

/-- Checked programs never use a freed object, free twice, or free a non-object: any function with no
parameters, run from the empty heap, for any fuel and any nondeterministic answers. -/
theorem core_sound (movedOnTrue : Bool) (P : Prog) (hP : checkProg (Rules.full movedOnTrue) P = true)
    (f : Nat) (fn : Fn) (hf : P[f]? = some fn) (hm : fn.params = []) (fuel : Nat) (orc : Nat → Bool) :
    run P f fuel orc ≠ .err := by
  let main : Fn := ⟨[], .unit, 0, .skip⟩
  have hk : chk (Rules.full movedOnTrue) P main (.call .none .shr f []) (fun _ => .dead) =
      some (some (prune fun _ => .dead)) := by
    have h1 : argSt (Rules.full movedOnTrue) .none [] (fun _ => .dead) = prune fun _ => .dead := by
      simp [argSt]
    simp [chk, callK, hf, hm, grd, assignK, untieD, rootsOk, h1]
  have G := exec_good P hP fuel _ main _ _ ⟨.empty, fun _ => none, fun _ => false, orc⟩ (fun _ => none)
    (fun _ => True) .empty hk (inv_empty _ _)
  intro e
  unfold run at e
  rw [e] at G; exact G

#print axioms core_sound

end SafeC.Core
