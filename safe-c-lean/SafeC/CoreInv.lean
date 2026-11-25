-- The core subset: heap forest, invariant, and the lemmas every command's proof is made of.
import SafeC.Core
namespace SafeC.Core

/-! ## Heaps -/

inductive Reach (h : Heap) : Nat → Nat → Prop
  | refl (a) : Reach h a a
  | step {a f c d} : h.fld a f = some c → Reach h c d → Reach h a d

theorem Reach.trans {h a b c} (p : Reach h a b) (q : Reach h b c) : Reach h a c := by
  induction p with
  | refl => exact q
  | step e _ ih => exact .step e (ih q)

theorem Reach.one {h : Heap} {a f c} (e : h.fld a f = some c) : Reach h a c := .step e (.refl _)

theorem Reach.tail {h a d} (p : Reach h a d) :
    a = d ∨ ∃ b f, Reach h a b ∧ h.fld b f = some d := by
  induction p with
  | refl => exact .inl rfl
  | @step a f c d e _ ih =>
    rcases ih with rfl | ⟨b, g, r, e'⟩
    · exact .inr ⟨a, f, .refl _, e⟩
    · exact .inr ⟨b, g, .step e r, e'⟩

structure WF (h : Heap) : Prop where
  lt : ∀ a f c, h.fld a f = some c → a < h.next ∧ c < h.next
  alt : ∀ a, h.alive a = true → a < h.next
  alc : ∀ a f c, h.alive a = true → h.fld a f = some c → h.alive c = true
  uniq : ∀ a f b g c, h.fld a f = some c → h.fld b g = some c → a = b ∧ f = g
  acyc : ∀ a f c, h.fld a f = some c → ¬ Reach h c a

theorem WF.anti {h a b} (w : WF h) (p : Reach h a b) (q : Reach h b a) : a = b := by
  cases p with
  | refl => rfl
  | step e r => exact absurd (r.trans q) (w.acyc _ _ _ e)

theorem WF.up {h a b d} (w : WF h) (p : Reach h a d) (q : Reach h b d) :
    Reach h a b ∨ Reach h b a := by
  induction p with
  | refl => exact .inr q
  | @step a f c d e r ih =>
    rcases ih q with h1 | h1
    · exact .inl (.step e h1)
    · rcases h1.tail with rfl | ⟨b', g, r', e'⟩
      · exact .inl (.one e)
      · obtain ⟨rfl, _⟩ := w.uniq _ _ _ _ _ e e'
        exact .inr r'

theorem WF.ral {h a b} (w : WF h) (p : Reach h a b) (ha : h.alive a = true) : h.alive b = true := by
  induction p with
  | refl => exact ha
  | step e _ ih => exact ih (w.alc _ _ _ ha e)

theorem WF.rlt {h a b} (w : WF h) (p : Reach h a b) (ha : a < h.next) : b < h.next := by
  induction p with
  | refl => exact ha
  | step e _ ih => exact ih (w.lt _ _ _ e).2

def Disj (h : Heap) (a b : Nat) : Prop := ∀ c, Reach h a c → Reach h b c → False

theorem Reach.frame {h h' : Heap} {b a} (p : Reach h b a)
    (H : ∀ n, Reach h b n → Reach h n a → n ≠ a → ∀ f, h'.fld n f = h.fld n f) : Reach h' b a := by
  induction p with
  | refl => exact .refl _
  | @step b f c d e r ih =>
    by_cases hb : b = d
    · subst hb; exact .refl _
    · refine .step (f := f) (c := c) ?_ (ih fun n h1 h2 h3 => H n (.step e h1) h2 h3)
      rw [H b (.refl _) (.step e r) hb, e]

theorem Reach.mono {h h' : Heap} {b a} (H : ∀ n f c, h'.fld n f = some c → h.fld n f = some c)
    (p : Reach h' b a) : Reach h b a := by
  induction p with
  | refl => exact .refl _
  | step e _ ih => exact .step (H _ _ _ e) ih

/-! ## The invariant -/

/-- The checker's state once the booleans are known. -/
def resolve (g : St) (bs : Nat → Bool) : St := fun x =>
  match g x with
  | .maybe b => if bs b then .dead else .own
  | k => k

def KLe (k l : K) : Prop :=
  k = .dead ∨ k = l ∨
    ∃ c p c' p', k = .bor c p ∧ l = .bor c' p' ∧ (c = true → c' = true) ∧ (∀ z ∈ p, z ∈ p') ∧ (p = [] ↔ p' = [])

theorem KLe.rfl {k} : KLe k k := .inr (.inl (Eq.refl _))

theorem KLe.trans {k l m} (p : KLe k l) (q : KLe l m) : KLe k m := by
  unfold KLe at *
  grind

theorem KLe.facts {k l} (p : KLe k l) (h : k.lv = true) :
    l.lv = true ∧ (k.ex = true → l.ex = true) ∧ (∀ z ∈ k.path, z ∈ l.path) ∧ (k.path = [] ↔ l.path = [])
      ∧ (k = .own ↔ l = .own) := by
  rcases p with rfl | rfl | ⟨c, p, c', p', rfl, rfl, h1, h2, h3⟩
  · cases h
  · simp_all
  · simp_all [K.lv, K.ex, K.path]

def Rt (g : St) : Prop := ∀ x, (g x).lv = true →
  (g (rootOf g x)).lv = true ∧ (g (rootOf g x)).path = [] ∧ ((g x).ex = true → (g (rootOf g x)).ex = true)

theorem getLastD_mem {l : List Nat} {d : Nat} : l.getLastD d = d ∧ l = [] ∨ l.getLastD d ∈ l := by
  cases l with
  | nil => exact .inl ⟨rfl, rfl⟩
  | cons a t =>
    right
    induction t generalizing a with
    | nil => simp
    | cons b t ih => have := ih b; simp [List.getLastD] at *

theorem root_clo (g : St) (x : Nat) : rootOf g x ∈ clo g x := by
  rcases getLastD_mem (l := (g x).path) (d := x) with ⟨h, _⟩ | h
  · rw [rootOf, h]; exact List.mem_cons_self
  · exact List.mem_cons_of_mem _ h

theorem root_of_nil {g : St} {x} (h : (g x).path = []) : rootOf g x = x := by simp [rootOf, h]

/-- `F`: the objects this frame may change; `H0`: the heap at entry. -/
structure Inv (np : Nat) (env0 : Nat → Option Nat) (F : Nat → Prop) (H0 : Heap)
    (g : St) (env : Nat → Option Nat) (h : Heap) : Prop where
  wf : WF h
  rt : Rt g
  anc : ∀ x q a, (g x).lv = true → q ∈ (g x).path → env x = some a → ∃ b, env q = some b ∧ Reach h b a
  al : ∀ x a, (g x).lv = true → env x = some a → h.alive a = true
  sep : ∀ x y a b, (g x).lv = true → (g y).lv = true → (g x).path = [] → (g y).path = [] → x ≠ y →
    (g x).ex = true → env x = some a → env y = some b → Disj h a b
  par : ∀ x a b f, g x = .own → env x = some a → h.fld b f ≠ some a
  parE : ∀ x a b f, (g x).lv = true → (g x).path = [] → (g x).ex = true → env x = some a → F b →
    h.fld b f ≠ some a
  inF : ∀ x a, (g x).lv = true → (g x).path = [] → (g x).ex = true → env x = some a → F a
  Fcl : ∀ a f c, F a → h.fld a f = some c → F c
  fr : ∀ a, ¬ F a → h.alive a = H0.alive a ∧ ∀ f, h.fld a f = H0.fld a f
  fresh : ∀ a, H0.next ≤ a → F a
  nx : H0.next ≤ h.next
  p0 : ∀ i, i < np → env i = env0 i

variable {np : Nat} {env0 : Nat → Option Nat} {F : Nat → Prop} {H0 : Heap} {g : St}
  {env : Nat → Option Nat} {h : Heap}

theorem Inv.anc' (I : Inv np env0 F H0 g env h) {x q a} (hx : (g x).lv = true) (hq : q ∈ clo g x)
    (ha : env x = some a) : ∃ b, env q = some b ∧ Reach h b a := by
  rcases List.mem_cons.mp hq with rfl | hq
  · exact ⟨a, ha, .refl _⟩
  · exact I.anc _ _ _ hx hq ha

theorem Inv.disj (I : Inv np env0 F H0 g env h) {x y a b} (hx : (g x).lv = true) (he : (g x).ex = true)
    (hy : (g y).lv = true) (hr : rootOf g x ≠ rootOf g y) (ha : env x = some a) (hb : env y = some b) :
    Disj h a b := by
  obtain ⟨x1, x2, x3⟩ := I.rt x hx
  obtain ⟨y1, y2, _⟩ := I.rt y hy
  obtain ⟨a', ea, ra⟩ := I.anc' hx (root_clo g x) ha
  obtain ⟨b', eb, rb⟩ := I.anc' hy (root_clo g y) hb
  intro c p q
  exact I.sep _ _ _ _ x1 y1 x2 y2 hr (x3 he) ea eb c (ra.trans p) (rb.trans q)

theorem ex_lv {k : K} (h : k.ex = true) : k.lv = true := by cases k <;> simp_all [K.ex, K.lv]

/-- What a command that mutates through the locals in `E` may change. -/
def Touch (env : Nat → Option Nat) (h : Heap) (E : Nat → Prop) (n : Nat) : Prop :=
  (∃ e a, E e ∧ env e = some a ∧ Reach h a n) ∨ h.next ≤ n

theorem touch_cl {E} (w : WF h) : ∀ a f c, Touch env h E a → h.fld a f = some c → Touch env h E c := by
  rintro a f c (⟨e, b, h1, h2, h3⟩ | h1) e'
  · exact .inl ⟨e, b, h1, h2, h3.trans (.one e')⟩
  · have := (w.lt _ _ _ e').1; omega

/-- A path in the new heap enters the touched part only at a touched local's own object. -/
theorem entry {E : Nat → Prop} (w : WF h) {h' : Heap}
    (un : ∀ n, ¬ Touch env h E n → h'.alive n = h.alive n ∧ ∀ f, h'.fld n f = h.fld n f)
    {a c} (p : Reach h' a c) : a < h.next → ¬ Touch env h E a →
    (¬ Touch env h E c ∧ Reach h a c) ∨
      ∃ e b, E e ∧ env e = some b ∧ Reach h a b ∧ Reach h' b c := by
  induction p with
  | refl a => intro _ ht; exact .inl ⟨ht, .refl _⟩
  | @step a f c d e r ih =>
    intro hlt ht
    rw [(un a ht).2 f] at e
    have hc := (w.lt _ _ _ e).2
    by_cases tc : Touch env h E c
    · rcases tc with ⟨l, b, h1, h2, h3⟩ | h1
      · rcases h3.tail with rfl | ⟨a', f', r', e'⟩
        · exact .inr ⟨l, b, h1, h2, .one e, r⟩
        · obtain ⟨rfl, _⟩ := w.uniq _ _ _ _ _ e e'
          exact absurd (.inl ⟨l, b, h1, h2, r'⟩) ht
      · omega
    · rcases ih hc tc with ⟨h1, h2⟩ | ⟨l, b, h1, h2, h3, h4⟩
      · exact .inl ⟨h1, .step e h2⟩
      · exact .inr ⟨l, b, h1, h2, .step e h3, h4⟩

/-- `entry` from a live root: the local entered through has that root. -/
theorem Inv.entryR (I : Inv np env0 F H0 g env h) {E : Nat → Prop} (hE : ∀ e, E e → (g e).ex = true)
    {h' : Heap}
    (un : ∀ n, ¬ Touch env h E n → h'.alive n = h.alive n ∧ ∀ f, h'.fld n f = h.fld n f)
    {r a c} (hr : (g r).lv = true) (hp : (g r).path = []) (ha : env r = some a) (p : Reach h' a c) :
    (¬ Touch env h E c ∧ Reach h a c) ∨
      ∃ e b, E e ∧ env e = some b ∧ Reach h a b ∧ Reach h' b c ∧ rootOf g e = r := by
  have key : ∀ e b, E e → env e = some b → ∀ d, Reach h a d → Reach h b d → rootOf g e = r := by
    intro e b h1 h2 d h3 h4
    refine Classical.byContradiction fun hn => ?_
    exact I.disj (ex_lv (hE e h1)) (hE e h1) hr (by rwa [root_of_nil hp]) h2 ha _ h4 h3
  have alt := I.wf.alt _ (I.al _ _ hr ha)
  by_cases ht : Touch env h E a
  · rcases ht with ⟨e, b, h1, h2, h3⟩ | h1
    · have hroot := key e b h1 h2 _ (.refl _) h3
      obtain ⟨b', h4, h5⟩ := I.anc' (ex_lv (hE e h1)) (root_clo g e) h2
      rw [hroot, ha] at h4; cases h4
      have := I.wf.anti h3 h5; subst this
      exact .inr ⟨e, b, h1, h2, .refl _, p, hroot⟩
    · omega
  · rcases entry I.wf un p alt ht with h1 | ⟨e, b, h1, h2, h3, h4⟩
    · exact .inl h1
    · exact .inr ⟨e, b, h1, h2, h3, h4, key e b h1 h2 _ h3 (.refl _)⟩

/-- The frame lemma: a change inside the objects reached from the locals in `E`. -/
theorem Inv.touch (I : Inv np env0 F H0 g env h) (E : Nat → Prop) (hE : ∀ e, E e → (g e).ex = true)
    {h' : Heap} (wf' : WF h') (nx' : h.next ≤ h'.next)
    (un : ∀ n, ¬ Touch env h E n → h'.alive n = h.alive n ∧ ∀ f, h'.fld n f = h.fld n f)
    (tcl : ∀ a f c, Touch env h E a → h'.fld a f = some c → Touch env h E c)
    {g' : St} (hle : ∀ x, KLe (g' x) (g x)) (rt' : Rt g')
    (hd : ∀ y e, E e → (g' y).lv = true → y ∈ clo g e ∨ rootOf g y ≠ rootOf g e)
    (keep : ∀ e, E e → (g' e).lv = true ∨ g e = .own)
    (post : ∀ e a, E e → (g' e).lv = true → env e = some a →
      h'.alive a = true ∧ ∀ b f, Touch env h E b → h'.fld b f ≠ some a)
    (psep : ∀ e1 e2 a b, E e1 → E e2 → e1 ≠ e2 → (g' e1).lv = true → (g' e2).lv = true →
      env e1 = some a → env e2 = some b → Disj h' a b) :
    Inv np env0 F H0 g' env h' := by
  have fx := fun x (hx : (g' x).lv = true) => (hle x).facts hx
  -- a touched local whose root survives, survives
  have keepR : ∀ e r, E e → rootOf g e = r → (g' r).lv = true → (g' e).lv = true := by
    intro e r he hr hl
    rcases keep e he with h1 | h1
    · exact h1
    · have : rootOf g e = e := root_of_nil (by rw [h1]; rfl)
      rw [← this, hr]; exact hl
  have TF : ∀ n, Touch env h E n → F n := by
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
  -- a surviving root's object has no parent in the touched part
  have noPar : ∀ r a b f, (g' r).lv = true → (g r).path = [] → env r = some a → Touch env h E b →
      h'.fld b f ≠ some a := by
    intro r a b f hr hp ha hb e
    have ta := tcl _ _ _ hb e
    have hr0 := (fx r hr).1
    rcases I.entryR hE (h' := h) (fun n _ => ⟨rfl, fun _ => rfl⟩) hr0 hp ha (.refl a) with ⟨h1, _⟩ | ⟨l, c, h1, h2, h3, h4, h5⟩
    · exact h1 ta
    · have hl := keepR l r h1 h5 hr
      have := I.wf.anti h3 h4; subst this
      exact (post l _ h1 hl h2).2 b f hb e
  refine
    { wf := wf', rt := rt', anc := ?_, al := ?_, sep := ?_, par := ?_, parE := ?_, inF := ?_, Fcl := ?_,
      fr := ?_, fresh := I.fresh, nx := Nat.le_trans I.nx nx', p0 := I.p0 }
  · intro y q a hy hq ha
    obtain ⟨y1, y2, y3, y4, y5⟩ := fx y hy
    obtain ⟨b, hb, rb⟩ := I.anc y q a y1 (y3 q hq) ha
    refine ⟨b, hb, rb.frame fun n n1 n2 n3 => (un n ?_).2⟩
    rintro (⟨e, c, h1, h2, h3⟩ | h1)
    · rcases hd y e h1 hy with hc | hc
      · obtain ⟨a', e1, e2⟩ := I.anc' (ex_lv (hE e h1)) hc h2
        rw [ha] at e1; cases e1
        exact n3 (I.wf.anti n2 (e2.trans h3))
      · exact I.disj (ex_lv (hE e h1)) (hE e h1) y1 (Ne.symm hc) h2 ha a (h3.trans n2) (.refl _)
    · have := I.wf.alt _ (I.al y a y1 ha)
      cases n2 with
      | refl => exact n3 rfl
      | step e' _ => have := (I.wf.lt _ _ _ e').1; omega
  · intro y a hy ha
    obtain ⟨y1, y2, y3, y4, y5⟩ := fx y hy
    by_cases ht : Touch env h E a
    · rcases ht with ⟨e, c, h1, h2, h3⟩ | h1
      · rcases hd y e h1 hy with hc | hc
        · obtain ⟨a', e1, e2⟩ := I.anc' (ex_lv (hE e h1)) hc h2
          rw [ha] at e1; cases e1
          have := I.wf.anti e2 h3; subst this
          have hl : (g' e).lv = true := by
            rcases keep e h1 with hk | hk
            · exact hk
            · have hc' : y = e := by simpa [clo, hk, K.path] using hc
              exact hc' ▸ hy
          exact (post e _ h1 hl h2).1
        · exact absurd (.refl _) (I.disj (ex_lv (hE e h1)) (hE e h1) y1 (Ne.symm hc) h2 ha a h3)
      · have := I.wf.alt _ (I.al y a y1 ha); omega
    · rw [(un a ht).1]; exact I.al y a y1 ha
  · intro x y a b hx hy px py hxy ex ea eb c p q
    obtain ⟨x1, x2, x3, x4, x5⟩ := fx x hx
    obtain ⟨y1, y2, y3, y4, y5⟩ := fx y hy
    have S := I.sep x y a b x1 y1 (x4.mp px) (y4.mp py) hxy (x2 ex) ea eb
    have tr : ∀ e b, E e → env e = some b → Reach h' b c → Touch env h E c := by
      intro e b h1 h2 h3
      have : Touch env h E b := .inl ⟨e, b, h1, h2, .refl _⟩
      clear h2
      induction h3 with
      | refl => exact this
      | step e' _ ih => exact ih (by assumption) (by assumption) (tcl _ _ _ this e')
    rcases I.entryR hE un x1 (x4.mp px) ea p with ⟨h1, h2⟩ | ⟨l, d, h1, h2, h3, h4, h5⟩
    · rcases I.entryR hE un y1 (y4.mp py) eb q with ⟨_, k2⟩ | ⟨l', d', k1, k2, k3, k4, k5⟩
      · exact S c h2 k2
      · exact h1 (tr _ _ k1 k2 k4)
    · rcases I.entryR hE un y1 (y4.mp py) eb q with ⟨k1, k2⟩ | ⟨l', d', k1, k2, k3, k4, k5⟩
      · exact k1 (tr _ _ h1 h2 h4)
      · have : l ≠ l' := by rintro rfl; exact hxy (h5.symm.trans k5)
        exact psep l l' d d' h1 k1 this (keepR _ _ h1 h5 hx) (keepR _ _ k1 k5 hy) h2 k2 c h4 k4
  · intro x a b f hx ha e
    have hx' : (g' x).lv = true := by rw [hx]; rfl
    obtain ⟨x1, x2, x3, x4, x5⟩ := fx x hx'
    have hp : (g x).path = [] := by rw [x5.mp hx]; rfl
    by_cases hb : Touch env h E b
    · exact noPar x a b f hx' hp ha hb e
    · rw [(un b hb).2 f] at e; exact I.par x a b f (x5.mp hx) ha e
  · intro x a b f hx hp he ha hb e
    obtain ⟨x1, x2, x3, x4, x5⟩ := fx x hx
    by_cases hb' : Touch env h E b
    · exact noPar x a b f hx (x4.mp hp) ha hb' e
    · rw [(un b hb').2 f] at e
      exact I.parE x a b f x1 (x4.mp hp) (x2 he) ha hb e
  · intro x a hx hp he ha
    obtain ⟨x1, x2, x3, x4, x5⟩ := fx x hx
    exact I.inF x a x1 (x4.mp hp) (x2 he) ha
  · intro a f c ha e
    by_cases ht : Touch env h E a
    · exact TF _ (tcl _ _ _ ht e)
    · rw [(un a ht).2 f] at e; exact I.Fcl _ _ _ ha e
  · intro a ha
    have ht : ¬ Touch env h E a := fun t => ha (TF _ t)
    rw [(un a ht).1]
    refine ⟨(I.fr a ha).1, fun f => ?_⟩
    rw [(un a ht).2 f]; exact (I.fr a ha).2 f

theorem Inv.mono (I : Inv np env0 F H0 g env h) {g' : St} (hle : ∀ x, KLe (g' x) (g x)) (rt' : Rt g') :
    Inv np env0 F H0 g' env h :=
  I.touch (fun _ => False) (fun _ h => h.elim) I.wf (Nat.le_refl _) (fun _ _ => ⟨rfl, fun _ => rfl⟩)
    (touch_cl I.wf) hle rt' (fun _ _ h => h.elim) (fun _ h => h.elim) (fun _ _ h => h.elim)
    (fun _ _ _ _ h => h.elim)

/-- `x` is in no ancestor list and not live. -/
def Gone (g : St) (x : Nat) : Prop := g x = .dead ∧ ∀ y, x ∉ (g y).path

theorem upd_same {α} (f : Nat → α) (x v) : upd f x v x = v := by simp [upd]
theorem upd_ne {α} (f : Nat → α) {x y} (v) (h : y ≠ x) : upd f x v y = f y := by simp [upd, h]

theorem rootOf_upd {x k y} (hy : y ≠ x) : rootOf (upd g x k) y = rootOf g y := by
  simp [rootOf, upd_ne _ _ hy]

theorem Inv.add (I : Inv np env0 F H0 g env h) {x : Nat} (hx : np ≤ x) (G : Gone g x) (k : K) (v : Option Nat)
    (_ : k.lv = true)
    (hrt : (upd g x k (k.path.getLastD x)).lv = true ∧ (upd g x k (k.path.getLastD x)).path = [] ∧
      (k.ex = true → (upd g x k (k.path.getLastD x)).ex = true))
    (hxp : x ∉ k.path)
    (hal : ∀ a, v = some a → h.alive a = true)
    (hanc : ∀ a q, v = some a → q ∈ k.path → ∃ b, env q = some b ∧ Reach h b a)
    (hown : k.path = [] → k = .own ∧ ∀ a, v = some a → F a ∧ (∀ b f, h.fld b f ≠ some a) ∧
      ∀ y b, (g y).lv = true → (g y).path = [] → env y = some b → Disj h a b) :
    Inv np env0 F H0 (upd g x k) (upd env x v) h := by
  have ne : ∀ y, (g y).lv = true → y ≠ x := by rintro y hy rfl; rw [G.1] at hy; cases hy
  refine { wf := I.wf, rt := ?_, anc := ?_, al := ?_, sep := ?_, par := ?_, parE := ?_, inF := ?_,
           Fcl := I.Fcl, fr := I.fr, fresh := I.fresh, nx := I.nx, p0 := ?_ }
  · intro y hy
    by_cases hyx : y = x
    · subst hyx
      simp only [rootOf, upd_same]; exact hrt
    · rw [upd_ne _ _ hyx] at hy ⊢
      rw [rootOf_upd hyx]
      obtain ⟨h1, h2, h3⟩ := I.rt y hy
      rw [upd_ne _ _ (ne _ h1)]; exact ⟨h1, h2, h3⟩
  · intro y q a hy hq ha
    by_cases hyx : y = x
    · subst hyx
      rw [upd_same] at hq ha
      have : q ≠ y := fun e => hxp (e ▸ hq)
      rw [upd_ne _ _ this]
      exact hanc a q ha hq
    · rw [upd_ne _ _ hyx] at hy hq ha
      have : q ≠ x := fun e => G.2 y (e ▸ hq)
      rw [upd_ne _ _ this]
      exact I.anc y q a hy hq ha
  · intro y a hy ha
    by_cases hyx : y = x
    · subst hyx; rw [upd_same] at ha; exact hal a ha
    · rw [upd_ne _ _ hyx] at hy ha; exact I.al y a hy ha
  · intro y z a b hy hz py pz hyz ey ea eb
    by_cases hyx : y = x
    · subst hyx
      rw [upd_same] at py ea
      rw [upd_ne _ _ (Ne.symm hyz)] at hz pz eb
      exact ((hown py).2 a ea).2.2 z b hz pz eb
    · rw [upd_ne _ _ hyx] at hy py ey ea
      by_cases hzx : z = x
      · subst hzx
        rw [upd_same] at pz eb
        intro c p q
        exact ((hown pz).2 b eb).2.2 y a hy py ea c q p
      · rw [upd_ne _ _ hzx] at hz pz eb
        exact I.sep y z a b hy hz py pz hyz ey ea eb
  · intro y a b f hy ha
    by_cases hyx : y = x
    · subst hyx
      rw [upd_same] at hy ha
      exact ((hown (by rw [hy]; rfl)).2 a ha).2.1 b f
    · rw [upd_ne _ _ hyx] at hy ha; exact I.par y a b f hy ha
  · intro y a b f hy hp he ha hb
    by_cases hyx : y = x
    · subst hyx
      rw [upd_same] at hp ha
      exact ((hown hp).2 a ha).2.1 b f
    · rw [upd_ne _ _ hyx] at hy hp he ha; exact I.parE y a b f hy hp he ha hb
  · intro y a hy hp he ha
    by_cases hyx : y = x
    · subst hyx
      rw [upd_same] at hp ha
      exact ((hown hp).2 a ha).1
    · rw [upd_ne _ _ hyx] at hy hp he ha; exact I.inF y a hy hp he ha
  · intro i hi
    rw [upd_ne _ _ (by omega)]; exact I.p0 i hi

theorem Inv.addOwn (I : Inv np env0 F H0 g env h) {x : Nat} (hx : np ≤ x) (G : Gone g x) (v : Option Nat)
    (H : ∀ a, v = some a → h.alive a = true ∧ F a ∧ (∀ b f, h.fld b f ≠ some a) ∧
      ∀ y b, (g y).lv = true → (g y).path = [] → env y = some b → Disj h a b) :
    Inv np env0 F H0 (upd g x .own) (upd env x v) h :=
  I.add hx G .own v rfl (by simp [K.path, upd_same, K.lv, K.ex]) (by simp [K.path])
    (fun a e => (H a e).1) (fun a q _ hq => by simp [K.path] at hq) (fun _ => ⟨rfl, fun a e => (H a e).2⟩)

/-- Assigning a gone local. -/
theorem Inv.setGone (I : Inv np env0 F H0 g env h) {x : Nat} (hx : np ≤ x) (G : Gone g x) (v : Option Nat) :
    Inv np env0 F H0 g (upd env x v) h := by
  have ne : ∀ y, (g y).lv = true → y ≠ x := by rintro y hy rfl; rw [G.1] at hy; cases hy
  have E : ∀ y, (g y).lv = true → upd env x v y = env y := fun y hy => upd_ne _ _ (ne y hy)
  refine { wf := I.wf, rt := I.rt, anc := ?_, al := ?_, sep := ?_, par := ?_, parE := ?_, inF := ?_,
           Fcl := I.Fcl, fr := I.fr, fresh := I.fresh, nx := I.nx, p0 := ?_ }
  · intro y q a hy hq ha
    rw [E y hy] at ha
    have : q ≠ x := fun e => G.2 y (e ▸ hq)
    rw [upd_ne _ _ this]; exact I.anc y q a hy hq ha
  · intro y a hy ha; rw [E y hy] at ha; exact I.al y a hy ha
  · intro y z a b hy hz py pz n ey ea eb
    rw [E y hy] at ea; rw [E z hz] at eb; exact I.sep y z a b hy hz py pz n ey ea eb
  · intro y a b f hy ha
    rw [E y (by rw [hy]; rfl)] at ha; exact I.par y a b f hy ha
  · intro y a b f hy hp he ha; rw [E y hy] at ha; exact I.parE y a b f hy hp he ha
  · intro y a hy hp he ha; rw [E y hy] at ha; exact I.inF y a hy hp he ha
  · intro i hi; rw [upd_ne _ _ (by omega)]; exact I.p0 i hi

end SafeC.Core
