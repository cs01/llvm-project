-- Soundness of the out-argument and view-array checker: a program it accepts
-- under the full rules never uses a freed block or a dead local and never
-- frees a block twice. Modular: the call case uses only the callee's
-- signature and the fact that its body passed the checker.
import SafeC.Out

namespace SafeC.Out

/-! ## Invariants -/

/-- What every state satisfies: an owner holds null or a live heap block of
the right kind, and no two owners hold the same one. -/
structure WF (st : St) : Prop where
  lt : ∀ b, st.alive b = true → b < st.next
  two : 2 ≤ st.next
  g0 : st.alive 0 = true ∧ st.kind 0 = .ocell
  g1 : st.kind 1 = .vcell ∧ st.cont 1 = .null
  own : ∀ c b, st.kind c = .ocell → st.alive c = true → st.cont c = .ptr b →
    st.alive b = true ∧ st.kind b = .hint
  view : ∀ c b, st.kind c = .vcell → st.alive c = true → st.cont c = .ptr b →
    st.alive b = true ∧ st.kind b = .hview
  uniq : ∀ c c' b, (st.kind c = .ocell ∨ st.kind c = .vcell) →
    (st.kind c' = .ocell ∨ st.kind c' = .vcell) → st.alive c = true → st.alive c' = true →
    st.cont c = .ptr b → st.cont c' = .ptr b → c = c'

/-- A frame: its types, where it starts, where its pointer parameters point,
and which blocks parameter `v` brought in at entry (`Q v b`). -/
structure Ctx where
  D : List Ty
  fp : Nat
  a : Nat → Nat
  Q : Nat → Nat → Prop

/-- The owner cell a variable names. -/
def OCell (X : Ctx) (v c : Nat) : Prop :=
  (X.D[v]? = some .own ∧ c = X.fp + v) ∨
  ((X.D[v]? = some .sh ∨ X.D[v]? = some .ex) ∧ c = X.a v)

/-- The cell whose borrow `A v` describes. -/
def CCell (X : Ctx) (st : St) (v c : Nat) : Prop :=
  (X.D[v]? = some .bor ∧ c = X.fp + v) ∨
  (X.D[v]? = some .vown ∧ st.cont (X.fp + v) = .ptr c) ∨
  (X.D[v]? = some .out ∧ c = X.a v)

/-- The owner cells and the borrow cells outside the frame that it may write. -/
def MC (X : Ctx) (c : Nat) : Prop := c = 0 ∨ ∃ v, X.D[v]? = some .ex ∧ c = X.a v
def WC (X : Ctx) (c : Nat) : Prop := ∃ v, X.D[v]? = some .out ∧ c = X.a v

/-- Block `b` is one that loan `l` stands for. -/
def Cov (X : Ctx) (st : St) : Loan → Nat → Prop
  | .own v, b => ∃ c, OCell X v c ∧ st.cont c = .ptr b
  | .addr v, b => X.D[v]? = some .int ∧ b = X.fp + v
  | .par v, b => X.Q v b ∧ b < X.fp ∧ ∀ c, MC X c → st.cont c ≠ .ptr b
  | .glob, b => st.cont 0 = .ptr b

def Good (X : Ctx) (st : St) (L : List Loan) (b : Nat) : Prop :=
  st.alive b = true ∧ (st.kind b = .hint ∨ st.kind b = .lint) ∧ ∃ l ∈ L, Cov X st l b

def GoodV (X : Ctx) (st : St) (o : Option (List Loan)) (v : Val) : Prop :=
  ∀ L b, o = some L → v = .ptr b → Good X st L b

structure Inv (X : Ctx) (A : Abs) (st : St) : Prop where
  wf : WF st
  fp2 : 2 ≤ X.fp
  le : X.fp ≤ st.next
  fr : ∀ v t, X.D[v]? = some t → st.alive (X.fp + v) = true ∧ st.kind (X.fp + v) = t.kind
  tgt : ∀ v, X.D[v]? = some .out ∨ X.D[v]? = some .sh ∨ X.D[v]? = some .ex →
    st.cont (X.fp + v) = .ptr (X.a v) ∧ st.alive (X.a v) = true ∧ X.a v < X.fp
  obj : ∀ v, X.D[v]? = some .sh ∨ X.D[v]? = some .ex → st.kind (X.a v) = .ocell ∧ X.a v ≠ 0
  outk : ∀ v, X.D[v]? = some .out → st.kind (X.a v) = .bvar ∨ st.kind (X.a v) = .hview
  apart : ∀ v w, X.D[v]? = some .ex → X.D[w]? = some .sh ∨ X.D[w]? = some .ex →
    v ≠ w → X.a v ≠ X.a w
  val : ∀ v c, CCell X st v c → GoodV X st (A v) (st.cont c)
  ali : ∀ q t L, X.D[q]? = some (.alias t) → X.D[t]? = some .vown → A q = some L →
    Loan.own t ∈ L ∧ (st.cont (X.fp + q) = .null ∨ st.cont (X.fp + q) = st.cont (X.fp + t))
  fresh : ∀ v b, X.D[v]? = some .own ∨ X.D[v]? = some .vown → st.cont (X.fp + v) = .ptr b →
    X.fp ≤ b

/-- What a step may do to the blocks below `n`: rewrite the owner cells in
`M` (with null or a fresh block), free what they held, and write the cells in
`W`. -/
structure Eff (M W : Nat → Prop) (n : Nat) (st st' : St) : Prop where
  nx : st.next ≤ st'.next
  kind : ∀ b, b < n → st'.kind b = st.kind b
  alive : ∀ b, b < n → st.alive b = true → st'.alive b = true ∨ ∃ c, M c ∧ st.cont c = .ptr b
  cont : ∀ b, b < n → ¬ M b → ¬ W b → st'.cont b = st.cont b
  mt : ∀ c b, M c → b < n → st'.cont c = .ptr b → st.cont c = .ptr b

theorem Eff.trans {M W : Nat → Prop} {n : Nat} {s1 s2 s3 : St}
    (h1 : Eff M W n s1 s2) (h2 : Eff M W n s2 s3) : Eff M W n s1 s3 where
  nx := Nat.le_trans h1.nx h2.nx
  kind b hb := by rw [h2.kind b hb, h1.kind b hb]
  alive b hb ha := by
    rcases h1.alive b hb ha with h | h
    · rcases h2.alive b hb h with h | ⟨c, hc, h⟩
      · exact .inl h
      · exact .inr ⟨c, hc, h1.mt c b hc hb h⟩
    · exact .inr h
  cont b hb hm hw := by rw [h2.cont b hb hm hw, h1.cont b hb hm hw]
  mt c b hc hb h := h1.mt c b hc hb (h2.mt c b hc hb h)

theorem Eff.refl (M W : Nat → Prop) (n : Nat) (st : St) : Eff M W n st st :=
  ⟨Nat.le_refl _, fun _ _ => rfl, fun _ _ h => .inl h, fun _ _ _ _ => rfl, fun _ _ _ _ h => h⟩

/-- The owner cells a step of this frame may touch. -/
def MOK (X : Ctx) (M : Nat → Prop) : Prop :=
  ∀ c, M c → MC X c ∨ ∃ v, (X.D[v]? = some .own ∨ X.D[v]? = some .vown) ∧ c = X.fp + v

/-- The other cells it may write. -/
def WOK (X : Ctx) (st : St) (W : Nat → Prop) : Prop :=
  ∀ c, W c → ((st.kind c = .bvar ∨ st.kind c = .hview) ∨
      ∃ q t, X.D[q]? = some (.alias t) ∧ c = X.fp + q) ∧ (WC X c ∨ X.fp ≤ c) ∧ st.alive c = true

/-- No loan in `L` stands for what a cell in `M` owns. -/
def NoKill (X : Ctx) (M : Nat → Prop) (L : List Loan) : Prop :=
  ∀ l ∈ L, match l with
    | .own v => ∀ c, OCell X v c → ¬ M c
    | .glob => ¬ M 0
    | _ => True

section
variable {X : Ctx} {A : Abs} {st : St}

theorem Inv.ocell (hI : Inv X A st) {v c : Nat} (h : OCell X v c) :
    st.kind c = .ocell ∧ st.alive c = true := by
  rcases h with ⟨h, rfl⟩ | ⟨h, rfl⟩
  · exact ⟨(hI.fr v _ h).2, (hI.fr v _ h).1⟩
  · exact ⟨(hI.obj v h).1, (hI.tgt v (.inr h)).2.1⟩

theorem Inv.ocell_inj (hI : Inv X A st) {v w c : Nat} (hv : OCell X v c) (hw : OCell X w c)
    (hx : X.D[v]? = some .own ∨ X.D[v]? = some .ex) : v = w := by
  rcases hv with ⟨hv, rfl⟩ | ⟨hv, rfl⟩
  · rcases hw with ⟨_, h⟩ | ⟨hw, h⟩
    · exact Nat.add_left_cancel h
    · have := (hI.tgt w (.inr hw)).2.2; omega
  · rcases hw with ⟨_, h⟩ | ⟨hw, h⟩
    · have := (hI.tgt v (.inr hv)).2.2; omega
    · rcases hx with hx | hx
      · rw [hx] at hv; simp at hv
      · exact Classical.byContradiction fun hne => hI.apart v w hx hw hne h

theorem Inv.ocell_ne0 (hI : Inv X A st) {v : Nat} (h : OCell X v 0) : False := by
  rcases h with ⟨_, h⟩ | ⟨h, h0⟩
  · have := hI.fp2; omega
  · exact (hI.obj v h).2 h0.symm

theorem Inv.mc (hI : Inv X A st) {c : Nat} (h : MC X c) :
    st.kind c = .ocell ∧ st.alive c = true ∧ c < X.fp := by
  have hfp := hI.fp2
  rcases h with rfl | ⟨v, h, rfl⟩
  · exact ⟨hI.wf.g0.2, hI.wf.g0.1, by omega⟩
  · exact ⟨(hI.obj v (.inr h)).1, (hI.tgt v (.inr (.inr h))).2⟩

theorem Inv.mok (hI : Inv X A st) {M : Nat → Prop} (hM : MOK X M) {c : Nat} (h : M c) :
    (st.kind c = .ocell ∨ st.kind c = .vcell) ∧ st.alive c = true := by
  rcases hM c h with hm | ⟨v, hv | hv, rfl⟩
  · exact ⟨.inl (hI.mc hm).1, (hI.mc hm).2.1⟩
  · exact ⟨.inl (hI.fr v _ hv).2, (hI.fr v _ hv).1⟩
  · exact ⟨.inr (hI.fr v _ hv).2, (hI.fr v _ hv).1⟩

/-- A cell in `M` holds only heap blocks. -/
theorem Inv.mheap (hI : Inv X A st) {M : Nat → Prop} (hM : MOK X M) {c b : Nat} (h : M c)
    (hb : st.cont c = .ptr b) : st.kind b = .hint ∨ st.kind b = .hview := by
  obtain ⟨hk | hk, ha⟩ := hI.mok hM h
  · exact .inl (hI.wf.own c b hk ha hb).2
  · exact .inr (hI.wf.view c b hk ha hb).2

/-- Below the frame, only cells in `MC` own what a step frees. -/
theorem Inv.mlow (hI : Inv X A st) {M : Nat → Prop} (hM : MOK X M) {c b : Nat} (h : M c)
    (hb : st.cont c = .ptr b) (hlt : b < X.fp) : MC X c := by
  rcases hM c h with hm | ⟨v, hv, rfl⟩
  · exact hm
  · have := hI.fresh v b hv hb; omega

theorem good_unowned (hI : Inv X A st) {M : Nat → Prop} (hM : MOK X M) {L : List Loan}
    (hk : NoKill X M L) {b : Nat} (hG : Good X st L b) : ∀ c, M c → st.cont c ≠ .ptr b := by
  intro c hc heq
  obtain ⟨_, hi, l, hl, hcov⟩ := hG
  obtain ⟨hko, hac⟩ := hI.mok hM hc
  have hlk := hk l hl
  cases l with
  | own v =>
    obtain ⟨c', ho, hc'⟩ := hcov
    have := hI.ocell ho
    have he := hI.wf.uniq c c' b hko (.inl this.1) hac this.2 heq hc'
    subst he; exact hlk c ho hc
  | addr v =>
    obtain ⟨hd, rfl⟩ := hcov
    have h1 := (hI.fr v _ hd).2
    have h2 := hI.mheap hM hc heq
    simp only [Ty.kind] at h1
    rw [h1] at h2; simp at h2
  | par v => exact hcov.2.2 c (hI.mlow hM hc heq hcov.2.1) heq
  | glob =>
    have he := hI.wf.uniq c 0 b hko (.inl hI.wf.g0.2) hac hI.wf.g0.1 heq hcov
    subst he; exact hlk hc

theorem good_eff (hI : Inv X A st) {M W : Nat → Prop} (hM : MOK X M) (hW : WOK X st W) {st' : St}
    (hE : Eff M W st.next st st') {L : List Loan} (hk : NoKill X M L) {b : Nat}
    (hG : Good X st L b) : Good X st' L b := by
  have hun := good_unowned hI hM hk hG
  obtain ⟨ha, hi, l, hl, hcov⟩ := hG
  have hb := hI.wf.lt b ha
  have hwo : ∀ c, st.kind c = .ocell → ¬ W c := by
    intro c hc hw
    rcases (hW c hw).1 with (h | h) | ⟨q, t, hq, rfl⟩
    · rw [hc] at h; cases h
    · rw [hc] at h; cases h
    · have := (hI.fr q _ hq).2; rw [hc] at this; cases this
  refine ⟨?_, by rw [hE.kind b hb]; exact hi, l, hl, ?_⟩
  · rcases hE.alive b hb ha with h | ⟨c, hc, h⟩
    · exact h
    · exact absurd h (hun c hc)
  · have hlk := hk l hl
    cases l with
    | own v =>
      obtain ⟨c, ho, hc⟩ := hcov
      have hoc := hI.ocell ho
      exact ⟨c, ho, by
        rw [hE.cont c (hI.wf.lt c hoc.2) (hlk c ho) (hwo c hoc.1)]; exact hc⟩
    | addr v => exact hcov
    | par v =>
      refine ⟨hcov.1, hcov.2.1, fun c hc heq => ?_⟩
      have hmc := hI.mc hc
      by_cases hm : M c
      · exact hcov.2.2 c hc (hE.mt c b hm hb heq)
      · rw [hE.cont c (hI.wf.lt c hmc.2.1) hm (hwo c hmc.1)] at heq
        exact hcov.2.2 c hc heq
    | glob =>
      show st'.cont 0 = _
      rw [hE.cont 0 (hI.wf.lt 0 hI.wf.g0.1) hlk (hwo 0 hI.wf.g0.2)]; exact hcov

theorem goodV_eff (hI : Inv X A st) {M W : Nat → Prop} (hM : MOK X M) (hW : WOK X st W) {st' : St}
    (hE : Eff M W st.next st st') {o : Option (List Loan)} (hk : ∀ L, o = some L → NoKill X M L)
    {v : Val} (hG : GoodV X st o v) : GoodV X st' o v :=
  fun L b ho hv => good_eff hI hM hW hE (hk L ho) (hG L b ho hv)

theorem Good.mono {st : St} {L L' : List Loan} {b : Nat} (h : Good X st L b)
    (hs : ∀ l ∈ L, l ∈ L') : Good X st L' b :=
  let ⟨ha, hi, l, hl, hc⟩ := h
  ⟨ha, hi, l, hs l hl, hc⟩

theorem Inv.alive_tgt (hI : Inv X A st) {M W : Nat → Prop} (hM : MOK X M) {st' : St}
    (hE : Eff M W st.next st st') {b : Nat} (ha : st.alive b = true)
    (hb : (st.kind b ≠ .hint ∧ st.kind b ≠ .hview) ∨ (st.kind b ≠ .hint ∧ b < X.fp)) :
    st'.alive b = true := by
  rcases hE.alive b (hI.wf.lt b ha) ha with h | ⟨c, hc, h⟩
  · exact h
  · exfalso
    rcases hb with hb | hb
    · exact (hI.mheap hM hc h).elim hb.1 hb.2
    · have hmc := hI.mc (hI.mlow hM hc h hb.2)
      exact hb.1 (hI.wf.own c b hmc.1 hmc.2.1 h).2

theorem Inv.not_w (hI : Inv X A st) {W : Nat → Prop} (hW : WOK X st W) {v : Nat} {t : Ty}
    (h : X.D[v]? = some t) (hb : t ≠ .bor) (ha : ∀ q, t ≠ .alias q) : ¬ W (X.fp + v) := by
  intro hw
  have hk := (hI.fr v t h).2
  rcases (hW _ hw).1 with (h' | h') | ⟨q, s, hq, he⟩
  · rw [hk] at h'; cases t <;> simp_all [Ty.kind]
  · rw [hk] at h'; cases t <;> simp_all [Ty.kind]
  · have : v = q := Nat.add_left_cancel he
    subst this; rw [h] at hq; cases hq; exact ha _ rfl

theorem Inv.not_m (hI : Inv X A st) {M : Nat → Prop} (hM : MOK X M) {v : Nat} {t : Ty}
    (h : X.D[v]? = some t) (hb : t ≠ .own) (ha : t ≠ .vown) : ¬ M (X.fp + v) := by
  intro hm
  have hk := (hI.fr v t h).2
  have := (hI.mok hM hm).1
  rw [hk] at this
  cases t <;> simp_all [Ty.kind]

/-- The invariant after a step, given the borrows it leaves. -/
theorem inv_eff (hI : Inv X A st) {M W : Nat → Prop} (hM : MOK X M) (hW : WOK X st W)
    {st' : St} {A' : Abs} (hwf : WF st') (hE : Eff M W st.next st st')
    (hval : ∀ v c, CCell X st' v c → GoodV X st' (A' v) (st'.cont c))
    (hali : ∀ q t L, X.D[q]? = some (.alias t) → X.D[t]? = some .vown → A' q = some L →
      Loan.own t ∈ L ∧
        (st'.cont (X.fp + q) = .null ∨ st'.cont (X.fp + q) = st'.cont (X.fp + t))) :
    Inv X A' st' ∧ Eff (MC X) (WC X) X.fp st st' := by
  have hlt : ∀ {b}, st.alive b = true → b < st.next := fun {b} h => hI.wf.lt b h
  refine ⟨⟨hwf, hI.fp2, Nat.le_trans hI.le hE.nx, fun v t h => ?_, fun v h => ?_, fun v h => ?_,
    fun v h => ?_, hI.apart, hval, hali, fun v b h hb => ?_⟩, ?_⟩
  · have hf := hI.fr v t h
    refine ⟨hI.alive_tgt hM hE hf.1 (.inl ?_), by rw [hE.kind _ (hlt hf.1)]; exact hf.2⟩
    rw [hf.2]; cases t <;> simp [Ty.kind]
  · have ht := hI.tgt v h
    have h3 : ∃ t, X.D[v]? = some t ∧ t ≠ .bor ∧ t ≠ .own ∧ t ≠ .vown ∧ ∀ q, t ≠ .alias q := by
      rcases h with h | h | h <;> exact ⟨_, h, by simp, by simp, by simp, by simp⟩
    obtain ⟨t, hd, h1, h2, h3, h4⟩ := h3
    refine ⟨?_, ?_, ht.2.2⟩
    · rw [hE.cont _ (hlt (hI.fr v t hd).1) (hI.not_m hM hd h2 h3) (hI.not_w hW hd h1 h4)]
      exact ht.1
    · refine hI.alive_tgt hM hE ht.2.1 (.inr ⟨?_, ht.2.2⟩)
      rcases h with h | h
      · rcases hI.outk v h with h | h <;> rw [h] <;> simp
      · rw [(hI.obj v h).1]; simp
  · have ho := hI.obj v h
    exact ⟨by rw [hE.kind _ (hlt (hI.tgt v (.inr h)).2.1)]; exact ho.1, ho.2⟩
  · rw [hE.kind _ (hlt (hI.tgt v (.inl h)).2.1)]; exact hI.outk v h
  · by_cases hlb : b < st.next
    · by_cases hm : M (X.fp + v)
      · exact hI.fresh v b h (hE.mt _ _ hm hlb hb)
      · have : ∃ t, X.D[v]? = some t ∧ t ≠ .bor ∧ ∀ q, t ≠ .alias q := by
          rcases h with h | h <;> exact ⟨_, h, by simp, by simp⟩
        obtain ⟨t, hd, h1, h2⟩ := this
        rw [hE.cont _ (hlt (hI.fr v t hd).1) hm (hI.not_w hW hd h1 h2)] at hb
        exact hI.fresh v b h hb
    · have := hI.le; omega
  · have hle := hI.le
    refine ⟨hE.nx, fun b hb => hE.kind b (by omega), fun b hb ha => ?_, fun b hb hm hw => ?_,
      fun c b hc hb h => ?_⟩
    · rcases hE.alive b (by omega) ha with h | ⟨c, hc, h⟩
      · exact .inl h
      · exact .inr ⟨c, hI.mlow hM hc h hb, h⟩
    · refine hE.cont b (by omega) (fun h => ?_) (fun h => ?_)
      · rcases hM b h with h | ⟨v, _, rfl⟩
        · exact hm h
        · omega
      · rcases (hW b h).2.1 with h | h
        · exact hw h
        · omega
    · have hmc := hI.mc hc
      by_cases hm : M c
      · exact hE.mt c b hm (by omega) h
      · rw [hE.cont c (hlt hmc.2.1) hm (fun hw => ?_)] at h
        · exact h
        · rcases (hW c hw).1 with (h' | h') | ⟨q, t, hq, rfl⟩
          · rw [hmc.1] at h'; cases h'
          · rw [hmc.1] at h'; cases h'
          · omega

/-! ## Writing a borrow cell -/

theorem wf_set (hw : WF st) {c : Nat} (hc : st.kind c ≠ .ocell ∧ st.kind c ≠ .vcell) (v : Val) :
    WF (st.set c v) := by
  have hne : ∀ d, (st.kind d = .ocell ∨ st.kind d = .vcell) → (st.set c v).cont d = st.cont d := by
    intro d hd
    have : d ≠ c := by rintro rfl; exact hd.elim hc.1 hc.2
    simp [St.set, upd, this]
  refine ⟨hw.lt, hw.two, hw.g0, ⟨hw.g1.1, by rw [hne 1 (.inr hw.g1.1)]; exact hw.g1.2⟩,
    fun d b hd ha h => ?_, fun d b hd ha h => ?_, fun d d' b hd hd' ha ha' h h' => ?_⟩
  · rw [hne d (.inl hd)] at h; exact hw.own d b hd ha h
  · rw [hne d (.inr hd)] at h; exact hw.view d b hd ha h
  · rw [hne d hd] at h; rw [hne d' hd'] at h'; exact hw.uniq d d' b hd hd' ha ha' h h'

theorem eff_set (st : St) (c : Nat) (v : Val) (n : Nat) :
    Eff (fun _ => False) (· = c) n st (st.set c v) :=
  ⟨Nat.le_refl _, fun _ _ => rfl, fun _ _ h => .inl h,
    fun b _ _ hb => by simp only [St.set, upd]; rw [ite_eq_right hb], fun _ _ h => h.elim⟩

theorem mok_false : MOK X fun _ => False := fun _ h => h.elim
theorem nokill_false (L : List Loan) : NoKill X (fun _ => False) L := by
  intro l _; cases l <;> simp

/-- Writing any cell that is not an owner keeps every borrow. -/
theorem goodV_set {X : Ctx} {A : Abs} {st : St} (hI : Inv X A st) {c : Nat} (hW : WOK X st (· = c)) (v : Val)
    {o : Option (List Loan)} {x : Val} (h : GoodV X st o x) : GoodV X (st.set c v) o x :=
  goodV_eff hI mok_false hW (eff_set st c v _) (fun L _ => nokill_false L) h

/-- The content cells do not move. -/
theorem ccell_set {X : Ctx} {st : St} {c : Nat} {v : Val} {w d : Nat}
    (hne : X.D[w]? = some .vown → X.fp + w ≠ c) (h : CCell X (st.set c v) w d) :
    CCell X st w d := by
  rcases h with h | ⟨hd, h⟩ | h
  · exact .inl h
  · refine .inr (.inl ⟨hd, ?_⟩)
    simpa [St.set, upd, hne hd] using h
  · exact .inr (.inr h)

theorem frNe {X : Ctx} {v w : Nat} {s t : Ty} (hv : X.D[v]? = some s)
    (hw : X.D[w]? = some t) (h : s ≠ t) : X.fp + v ≠ X.fp + w := by
  intro he
  have : v = w := Nat.add_left_cancel he
  subst this; rw [hv] at hw; cases hw; exact h rfl

/-- Two content cells that coincide. -/
theorem Inv.ccell_inj (hI : Inv X A st) {v w c : Nat} (hv : CCell X st v c) (hw : CCell X st w c) :
    v = w ∨ (X.D[v]? = some .out ∧ X.D[w]? = some .out) := by
  have key : ∀ {v w}, X.D[v]? = some .vown → st.cont (X.fp + v) = .ptr c →
      (X.D[w]? = some .bor ∧ c = X.fp + w) → False := by
    intro v w hd h ⟨hb, he⟩
    have h1 := (hI.wf.view _ _ (hI.fr v _ hd).2 (hI.fr v _ hd).1 h).2
    have h2 := (hI.fr w _ hb).2
    rw [he, h2] at h1; cases h1
  have key2 : ∀ {v w}, X.D[v]? = some .vown → st.cont (X.fp + v) = .ptr c →
      (X.D[w]? = some .out ∧ c = X.a w) → False := by
    intro v w hd h ⟨hb, he⟩
    have h1 := hI.fresh v c (.inr hd) h
    have h2 := (hI.tgt w (.inl hb)).2.2
    omega
  have key3 : ∀ {v w}, (X.D[v]? = some .bor ∧ c = X.fp + v) →
      (X.D[w]? = some .out ∧ c = X.a w) → False := by
    intro v w ⟨_, h⟩ ⟨hb, he⟩
    have h2 := (hI.tgt w (.inl hb)).2.2
    omega
  rcases hv with hv | ⟨hv, hv'⟩ | hv <;> rcases hw with hw | ⟨hw, hw'⟩ | hw
  · exact .inl (Nat.add_left_cancel (hv.2.symm.trans hw.2))
  · exact (key hw hw' hv).elim
  · exact (key3 hv hw).elim
  · exact (key hv hv' hw).elim
  · have := hI.wf.uniq _ _ c (.inr (hI.fr v _ hv).2) (.inr (hI.fr w _ hw).2)
      (hI.fr v _ hv).1 (hI.fr w _ hw).1 hv' hw'
    exact .inl (Nat.add_left_cancel this)
  · exact (key2 hv hv' hw).elim
  · exact (key3 hw hv).elim
  · exact (key2 hw hw' hv).elim
  · exact .inr ⟨hv.1, hw.1⟩

/-- Writing a content cell `c` of `q`. -/
theorem inv_store (hI : Inv X A st) {q c : Nat} (hc : CCell X st q c) (hal : st.alive c = true)
    {x : Val} {A' : Abs}
    (hA : ∀ v L, A' v = some L → ∃ M, (∀ l ∈ M, l ∈ L) ∧
      ((CCell X st v c ∧ GoodV X st (some M) x) ∨ (¬ CCell X st v c ∧ A v = some M)))
    (hq : ∀ q t, X.D[q]? = some (.alias t) → A' q = A q) :
    Inv X A' (st.set c x) ∧ Eff (MC X) (WC X) X.fp st (st.set c x) := by
  have hk : st.kind c = .bvar ∨ st.kind c = .hview := by
    rcases hc with ⟨h, rfl⟩ | ⟨h, h'⟩ | ⟨h, rfl⟩
    · exact .inl (hI.fr q _ h).2
    · exact .inr (hI.wf.view _ _ (hI.fr q _ h).2 (hI.fr q _ h).1 h').2
    · exact hI.outk q h
  have hW : WOK X st (· = c) := by
    rintro d rfl
    refine ⟨.inl hk, ?_, hal⟩
    rcases hc with ⟨h, rfl⟩ | ⟨h, h'⟩ | ⟨h, rfl⟩
    · exact .inr (Nat.le_add_right _ _)
    · exact .inr (hI.fresh q _ (.inr h) h')
    · exact .inl ⟨q, h, rfl⟩
  have hne : ∀ {w : Nat} {t : Ty}, X.D[w]? = some t → t.kind ≠ .bvar → t.kind ≠ .hview →
      X.fp + w ≠ c := by
    rintro w t hw h1 h2 rfl
    rw [(hI.fr w t hw).2] at hk
    exact hk.elim h1 h2
  refine inv_eff hI mok_false hW (wf_set hI.wf ?_ x) (eff_set st c x _) (fun v d hd => ?_)
    (fun q t L h1 h2 h3 => ?_)
  · rcases hk with h | h <;> rw [h] <;> simp
  · replace hd := ccell_set (fun h => hne h (by simp [Ty.kind]) (by simp [Ty.kind])) hd
    intro L b hL hb
    obtain ⟨M, hs, h⟩ := hA v L hL
    refine Good.mono ?_ hs
    rcases h with ⟨hv, hg⟩ | ⟨hv, hm⟩
    · have : d = c := by
        rcases hv with hv | hv | hv <;> rcases hd with hd | hd | hd <;>
          simp_all
      subst this
      simp only [St.set, upd, ite_true] at hb
      exact goodV_set hI hW x hg M b rfl hb
    · have : d ≠ c := by rintro rfl; exact hv ‹_›
      simp only [St.set, upd, ite_eq_right this] at hb
      exact goodV_set hI hW x (hI.val v d hd) M b hm hb
  · rw [hq q t h1] at h3
    have := hI.ali q t L h1 h2 h3
    simpa [St.set, upd, hne h1 (by simp [Ty.kind]) (by simp [Ty.kind]),
      hne h2 (by simp [Ty.kind]) (by simp [Ty.kind])] using this

/-- Writing a plain borrow variable. -/
theorem inv_setBor (hI : Inv X A st) {x : Nat} (hx : X.D[x]? = some .bor) {v : Val}
    {o : Option (List Loan)} (hv : GoodV X st o v) :
    Inv X (upd A x o) (st.set (X.fp + x) v) ∧
      Eff (MC X) (WC X) X.fp st (st.set (X.fp + x) v) := by
  have hc : CCell X st x (X.fp + x) := .inl ⟨hx, rfl⟩
  refine inv_store hI hc (hI.fr x _ hx).1 (fun w L hL => ⟨L, fun _ h => h, ?_⟩) (fun q t hq => ?_)
  · by_cases hw : w = x
    · subst hw
      simp only [upd, ite_true] at hL
      subst hL
      exact .inl ⟨hc, hv⟩
    · simp only [upd, ite_eq_right hw] at hL
      refine .inr ⟨fun h => ?_, hL⟩
      rcases hI.ccell_inj h hc with h | ⟨_, h⟩
      · exact hw h
      · rw [hx] at h; cases h
  · have : q ≠ x := by rintro rfl; rw [hx] at hq; cases hq
    simp [upd, this]

/-! ## Owner cells -/

theorem wf_alloc (hw : WF st) {c : Nat} {k : Kind} (ha : st.alive c = true) (hc1 : c ≠ 1)
    (hk : (st.kind c = .ocell ∧ k = .hint) ∨ (st.kind c = .vcell ∧ k = .hview)) :
    WF ((st.grow k).set c (.ptr st.next)) := by
  have hcl := hw.lt c ha
  have h0l := hw.lt 0 hw.g0.1
  have e1 : ∀ d, d ≠ st.next → (st.grow k).kind d = st.kind d := by
    intro d h; simp [St.grow, upd, h]
  have e2 : ∀ d, d ≠ st.next → (st.grow k).alive d = st.alive d := by
    intro d h; simp [St.grow, upd, h]
  have e3 : ∀ d, d ≠ c → d ≠ st.next → ((st.grow k).set c (.ptr st.next)).cont d = st.cont d := by
    intro d h h'; simp [St.set, St.grow, upd, h, h']
  have e4 : ((st.grow k).set c (.ptr st.next)).cont c = .ptr st.next := by simp [St.set, upd]
  have e5 : (st.grow k).kind st.next = k := by simp [St.grow, upd]
  have e6 : (st.grow k).alive st.next = true := by simp [St.grow, upd]
  have nk : k ≠ .ocell ∧ k ≠ .vcell := by rcases hk with ⟨_, rfl⟩ | ⟨_, rfl⟩ <;> simp
  have old : ∀ d, ((st.grow k).kind d = .ocell ∨ (st.grow k).kind d = .vcell) → d ≠ st.next := by
    rintro d h rfl; rw [e5] at h; exact h.elim nk.1 nk.2
  have old2 : ∀ d b, ((st.grow k).kind d = .ocell ∨ (st.grow k).kind d = .vcell) →
      (st.grow k).alive d = true → ((st.grow k).set c (.ptr st.next)).cont d = .ptr b →
      (d = c ∧ b = st.next) ∨ (d ≠ c ∧ st.cont d = .ptr b ∧ b ≠ st.next) := by
    intro d b hd hda h
    have hdn := old d hd
    by_cases hdc : d = c
    · subst hdc; rw [e4] at h; cases h; exact .inl ⟨rfl, rfl⟩
    · rw [e3 d hdc hdn] at h
      rw [e1 d hdn] at hd; rw [e2 d hdn] at hda
      refine .inr ⟨hdc, h, ?_⟩
      have : st.alive b = true := hd.elim (fun hd => (hw.own d b hd hda h).1)
        (fun hd => (hw.view d b hd hda h).1)
      have := hw.lt b this; omega
  have h2 := hw.two
  refine ⟨fun b h => ?_, Nat.le_succ_of_le h2, ?_, ?_, fun d b hd hda h => ?_, fun d b hd hda h => ?_,
    fun d d' b hd hd' hda hda' h h' => ?_⟩
  · by_cases hb : b = st.next
    · subst hb; exact Nat.lt_succ_self _
    · have : st.alive b = true := by rw [← e2 b hb]; exact h
      exact Nat.lt_succ_of_lt (hw.lt b this)
  · exact ⟨by show (st.grow k).alive 0 = true; rw [e2 0 (by omega)]; exact hw.g0.1,
      by show (st.grow k).kind 0 = _; rw [e1 0 (by omega)]; exact hw.g0.2⟩
  · have hn : (1 : Nat) ≠ st.next := by omega
    exact ⟨by show (st.grow k).kind 1 = _; rw [e1 1 hn]; exact hw.g1.1,
      by rw [e3 1 (Ne.symm hc1) hn]; exact hw.g1.2⟩
  · have hdn := old d (.inl hd)
    have hd' : st.kind d = .ocell := by rw [← e1 d hdn]; exact hd
    rcases old2 d b (.inl hd) hda h with ⟨rfl, rfl⟩ | ⟨_, h, hb⟩
    · refine ⟨e6, e5.trans ?_⟩
      rcases hk with ⟨_, rfl⟩ | ⟨h, _⟩
      · rfl
      · rw [hd'] at h; cases h
    · have hda' : st.alive d = true := by rw [← e2 d hdn]; exact hda
      have := hw.own d b hd' hda' h
      exact ⟨(e2 b hb).trans this.1, (e1 b hb).trans this.2⟩
  · have hdn := old d (.inr hd)
    have hd' : st.kind d = .vcell := by rw [← e1 d hdn]; exact hd
    rcases old2 d b (.inr hd) hda h with ⟨rfl, rfl⟩ | ⟨_, h, hb⟩
    · refine ⟨e6, e5.trans ?_⟩
      rcases hk with ⟨h, _⟩ | ⟨_, rfl⟩
      · rw [hd'] at h; cases h
      · rfl
    · have hda' : st.alive d = true := by rw [← e2 d hdn]; exact hda
      have := hw.view d b hd' hda' h
      exact ⟨(e2 b hb).trans this.1, (e1 b hb).trans this.2⟩
  · have hdn := old d hd
    have hdn' := old d' hd'
    rcases old2 d b hd hda h with ⟨rfl, hb⟩ | ⟨_, h1, hb⟩ <;>
      rcases old2 d' b hd' hda' h' with ⟨h3, hb'⟩ | ⟨_, h2, hb'⟩
    · exact h3.symm
    · exact absurd hb hb'
    · exact absurd hb' hb
    · refine hw.uniq d d' b ?_ ?_ ((e2 d hdn).symm.trans hda) ((e2 d' hdn').symm.trans hda') h1 h2
      · rw [← e1 d hdn]; exact hd
      · rw [← e1 d' hdn']; exact hd'

theorem eff_alloc {st : St} {c : Nat} {k : Kind} :
    Eff (· = c) (fun _ => False) st.next st ((st.grow k).set c (.ptr st.next)) := by
  have hn : ∀ b, b < st.next → b ≠ st.next := fun b h => Nat.ne_of_lt h
  refine ⟨Nat.le_succ _, fun b hb => by simp [St.set, St.grow, upd, hn b hb],
    fun b hb h => .inl (by simp [St.set, St.grow, upd, hn b hb, h]),
    fun b hb hc _ => by simp [St.set, St.grow, upd, hn b hb, hc], fun d b hd hb h => ?_⟩
  subst hd
  simp only [St.set, upd, ite_true, Val.ptr.injEq] at h
  omega

theorem wf_free (hw : WF st) {c b : Nat} (ha : st.alive c = true)
    (hk : st.kind c = .ocell ∨ st.kind c = .vcell) (hc : st.cont c = .ptr b) :
    WF ((st.kill b).set c .null) := by
  have hbk : st.kind b = .hint ∨ st.kind b = .hview := by
    rcases hk with hk | hk
    · exact .inl (hw.own c b hk ha hc).2
    · exact .inr (hw.view c b hk ha hc).2
  have e2 : ∀ d, d ≠ b → (st.kill b).alive d = st.alive d := by
    intro d h; simp [St.kill, upd, h]
  have eb : (st.kill b).alive b = false := by simp [St.kill, upd]
  have e2' : ∀ d, (st.kill b).alive d = true → st.alive d = true := by
    intro d h
    have : d ≠ b := by rintro rfl; rw [eb] at h; cases h
    rw [← e2 d this]; exact h
  have key : ∀ d e, (st.kind d = .ocell ∨ st.kind d = .vcell) → (st.kill b).alive d = true →
      ((st.kill b).set c .null).cont d = .ptr e →
      st.cont d = .ptr e ∧ ((st.kill b).alive e = st.alive e) := by
    intro d e hd hda h
    by_cases hdc : d = c
    · subst hdc; simp [St.set, upd] at h
    · replace h : st.cont d = .ptr e := by simpa [St.set, St.kill, upd, hdc] using h
      refine ⟨h, e2 e ?_⟩
      rintro rfl
      exact hdc (hw.uniq d c e hd hk (e2' d hda) ha h hc)
  have h0 : 0 ≠ b := by rintro rfl; rw [hw.g0.2] at hbk; simp at hbk
  refine ⟨fun d h => hw.lt d (e2' d h), hw.two, ⟨(e2 0 h0).trans hw.g0.1, hw.g0.2⟩,
    ⟨hw.g1.1, ?_⟩, fun d e hd hda h => ?_, fun d e hd hda h => ?_,
    fun d d' e hd hd' hda hda' h h' => ?_⟩
  · by_cases h : 1 = c
    · subst h; simp [St.set, upd]
    · simpa [St.set, St.kill, upd, h] using hw.g1.2
  · obtain ⟨h, he⟩ := key d e (.inl hd) hda h
    have := hw.own d e hd (e2' d hda) h
    exact ⟨he.trans this.1, this.2⟩
  · obtain ⟨h, he⟩ := key d e (.inr hd) hda h
    have := hw.view d e hd (e2' d hda) h
    exact ⟨he.trans this.1, this.2⟩
  · exact hw.uniq d d' e hd hd' (e2' d hda) (e2' d' hda') (key d e hd hda h).1
      (key d' e hd' hda' h').1

theorem eff_free {c b n : Nat} (hc : st.cont c = .ptr b) :
    Eff (· = c) (fun _ => False) n st ((st.kill b).set c .null) := by
  refine ⟨Nat.le_refl _, fun _ _ => rfl, fun d _ h => ?_,
    fun d _ hd _ => by simp [St.set, St.kill, upd, hd], fun d e hd _ h => ?_⟩
  · by_cases hdb : d = b
    · subst hdb; exact .inr ⟨c, rfl, hc⟩
    · exact .inl (by simp [St.set, St.kill, upd, hdb, h])
  · subst hd; simp [St.set, upd] at h

theorem kill_some {l : Loan} {v : Nat} {L : List Loan} (h : kill l A v = some L) :
    A v = some L ∧ l ∉ L := by
  unfold kill at h
  split at h
  · rename_i M hM
    split at h
    · cases h
    · cases h; exact ⟨hM, by assumption⟩
  · cases h

theorem wok_false : WOK X st fun _ => False := fun _ h => h.elim

/-- The owner cell that `l` names, and only it. -/
def Names (X : Ctx) (l : Loan) (c : Nat) : Prop :=
  match l with
  | .own v => OCell X v c ∧ (X.D[v]? = some .own ∨ X.D[v]? = some .ex)
  | .glob => c = 0
  | _ => False

/-- Rewriting one int owner cell ends the borrows of what it held. -/
theorem inv_owner (hI : Inv X A st) {l : Loan} {c : Nat} (hl : Names X l c) {st' : St}
    (hwf : WF st') (hE : Eff (· = c) (fun _ => False) st.next st st') :
    Inv X (kill l A) st' ∧ Eff (MC X) (WC X) X.fp st st' := by
  have hck : st.kind c = .ocell := by
    cases l with
    | own v => exact (hI.ocell hl.1).1
    | glob => have : c = 0 := hl; subst this; exact hI.wf.g0.2
    | _ => exact hl.elim
  have hM : MOK X (· = c) := by
    rintro d rfl
    cases l with
    | own v =>
      rcases hl with ⟨⟨h, h'⟩ | ⟨h, h'⟩, hx⟩
      · exact .inr ⟨v, .inl h, h'⟩
      · rcases hx with hx | hx
        · rw [hx] at h; simp at h
        · exact .inl (.inr ⟨v, hx, h'⟩)
    | glob => exact .inl (.inl hl)
    | _ => exact hl.elim
  have hnk : ∀ L, l ∉ L → NoKill X (· = c) L := by
    intro L hL m hm
    cases m with
    | own w =>
      rintro d ho rfl
      cases l with
      | own v =>
        have := hI.ocell_inj hl.1 ho hl.2
        subst this; exact hL hm
      | glob => have : d = 0 := hl; subst this; exact hI.ocell_ne0 ho
      | _ => exact hl.elim
    | glob =>
      rintro rfl
      cases l with
      | own v => exact hI.ocell_ne0 hl.1
      | glob => exact hL hm
      | _ => exact hl.elim
    | _ => trivial
  have hne : ∀ {w : Nat} {t : Ty}, X.D[w]? = some t → t ≠ .own → ¬ X.fp + w = c := by
    rintro w t hw ht rfl
    rw [(hI.fr w t hw).2] at hck; cases t <;> simp_all [Ty.kind]
  have hun : ∀ {w : Nat} {t : Ty}, X.D[w]? = some t → t ≠ .own →
      st'.cont (X.fp + w) = st.cont (X.fp + w) := fun {w t} hw ht =>
    hE.cont _ (hI.wf.lt _ (hI.fr w t hw).1) (hne hw ht) (fun h => h)
  refine inv_eff hI hM wok_false hwf hE (fun v d hd L b hL hb => ?_) (fun q t L h1 h2 h3 => ?_)
  · have hd' : CCell X st v d := by
      rcases hd with h | ⟨h, h'⟩ | h
      · exact .inl h
      · rw [hun h (by simp)] at h'; exact .inr (.inl ⟨h, h'⟩)
      · exact .inr (.inr h)
    obtain ⟨hA, hlL⟩ := kill_some hL
    have hdk : (st.kind d = .bvar ∨ st.kind d = .hview) ∧ st.alive d = true := by
      rcases hd' with ⟨h, rfl⟩ | ⟨h, h'⟩ | ⟨h, rfl⟩
      · exact ⟨.inl (hI.fr v _ h).2, (hI.fr v _ h).1⟩
      · have := hI.wf.view _ _ (hI.fr v _ h).2 (hI.fr v _ h).1 h'
        exact ⟨.inr this.2, this.1⟩
      · exact ⟨hI.outk v h, (hI.tgt v (.inl h)).2.1⟩
    have hdc : ¬ d = c := by rintro rfl; rw [hck] at hdk; simp at hdk
    rw [hE.cont d (hI.wf.lt d hdk.2) hdc (fun h => h)] at hb
    exact good_eff hI hM wok_false hE (hnk L hlL) (hI.val v d hd' L b hA hb)
  · rw [hun h1 (by simp), hun h2 (by simp)]
    exact hI.ali q t L h1 h2 (kill_some h3).1

/-- Rewriting a local pointer to views ends what its aliases point to. -/
theorem inv_vowner (hI : Inv X A st) {t : Nat} (ht : X.D[t]? = some .vown) {st' : St} {A' : Abs}
    (hwf : WF st') (hE : Eff (· = X.fp + t) (fun _ => False) st.next st st')
    (hA : ∀ v L, v ≠ t → A' v = some L → A v = some L)
    (hc : st'.cont (X.fp + t) = .null ∨ (st'.cont (X.fp + t) = .ptr st.next ∧
      st'.cont st.next = .null))
    (hq : ∀ q L, X.D[q]? = some (.alias t) → A' q = some L → Loan.own t ∉ L) :
    Inv X A' st' ∧ Eff (MC X) (WC X) X.fp st st' := by
  have hM : MOK X (· = X.fp + t) := by rintro d rfl; exact .inr ⟨t, .inr ht, rfl⟩
  have hck := hI.fr t _ ht
  have hnk : ∀ L, NoKill X (· = X.fp + t) L := by
    intro L m _
    cases m with
    | own w =>
      rintro d ho rfl
      have := (hI.ocell ho).1; rw [hck.2] at this; cases this
    | glob =>
      intro h
      have := hI.fp2
      show False
      omega
    | _ => trivial
  have hun : ∀ {w : Nat} {s : Ty}, X.D[w]? = some s → w ≠ t →
      st'.cont (X.fp + w) = st.cont (X.fp + w) := fun {w s} hw hne =>
    hE.cont _ (hI.wf.lt _ (hI.fr w s hw).1)
      (fun (h : X.fp + w = X.fp + t) => hne (Nat.add_left_cancel h)) (fun h => h)
  refine inv_eff hI hM wok_false hwf hE (fun v d hd L b hL hb => ?_) (fun q s L h1 h2 h3 => ?_)
  · by_cases hvt : v = t
    · subst hvt
      rcases hd with ⟨h, _⟩ | ⟨h, h'⟩ | ⟨h, _⟩
      · rw [ht] at h; cases h
      · rcases hc with hc | ⟨hc, hn⟩
        · rw [hc] at h'; cases h'
        · rw [hc] at h'; cases h'; rw [hn] at hb; cases hb
      · rw [ht] at h; cases h
    · have hd' : CCell X st v d := by
        rcases hd with h | ⟨h, h'⟩ | h
        · exact .inl h
        · rw [hun h hvt] at h'; exact .inr (.inl ⟨h, h'⟩)
        · exact .inr (.inr h)
      have hdk : (st.kind d = .bvar ∨ st.kind d = .hview) ∧ st.alive d = true := by
        rcases hd' with ⟨h, rfl⟩ | ⟨h, h'⟩ | ⟨h, rfl⟩
        · exact ⟨.inl (hI.fr v _ h).2, (hI.fr v _ h).1⟩
        · have := hI.wf.view _ _ (hI.fr v _ h).2 (hI.fr v _ h).1 h'
          exact ⟨.inr this.2, this.1⟩
        · exact ⟨hI.outk v h, (hI.tgt v (.inl h)).2.1⟩
      have hdc : ¬ d = X.fp + t := by rintro rfl; rw [hck.2] at hdk; simp [Ty.kind] at hdk
      rw [hE.cont d (hI.wf.lt d hdk.2) hdc (fun h => h)] at hb
      exact good_eff hI hM wok_false hE (hnk L) (hI.val v d hd' L b (hA v L hvt hL) hb)
  · have hqt : q ≠ t := by rintro rfl; rw [ht] at h1; cases h1
    by_cases hst : s = t
    · subst hst; exact absurd (hI.ali q s L h1 h2 (hA q L hqt h3)).1 (hq q L h1 h3)
    · rw [hun h1 hqt, hun h2 hst]
      exact hI.ali q s L h1 h2 (hA q L hqt h3)

end

/-! ## Entering and leaving a frame -/

section
variable {st : St} {D : List Ty} {vals : List Val}

theorem enter_old {b : Nat} (hb : b < st.next) :
    (enter st D vals).alive b = st.alive b ∧ (enter st D vals).cont b = st.cont b ∧
    (enter st D vals).kind b = st.kind b := by
  have : ¬ (st.next ≤ b ∧ b < st.next + D.length) := by omega
  simp [enter, this]

theorem enter_new {i : Nat} {t : Ty} (h : D[i]? = some t) :
    (enter st D vals).alive (st.next + i) = true ∧
    (enter st D vals).cont (st.next + i) = (vals[i]?).getD .null ∧
    (enter st D vals).kind (st.next + i) = t.kind := by
  have hi : i < D.length := (List.getElem?_eq_some_iff.mp h).1
  have : st.next ≤ st.next + i ∧ st.next + i < st.next + D.length := by omega
  simp [enter, this, h]

/-- A live block after `enter` is an old one or a variable. -/
theorem enter_cases (hw : WF st) {b : Nat} (h : (enter st D vals).alive b = true) :
    b < st.next ∨ ∃ i t, D[i]? = some t ∧ b = st.next + i := by
  by_cases hb : st.next ≤ b ∧ b < st.next + D.length
  · exact .inr ⟨b - st.next, D[b - st.next], List.getElem?_eq_getElem (by omega), by omega⟩
  · simp only [enter, ite_eq_right hb] at h
    exact .inl (hw.lt b h)

theorem wf_enter (hw : WF st)
    (hv : ∀ i : Nat, D[i]? = some .own ∨ D[i]? = some .vown → vals[i]? = none) :
    WF (enter st D vals) := by
  have h2 := hw.two
  have key : ∀ c b, ((enter st D vals).kind c = .ocell ∨ (enter st D vals).kind c = .vcell) →
      (enter st D vals).alive c = true → (enter st D vals).cont c = .ptr b →
      c < st.next ∧ st.kind c = (enter st D vals).kind c ∧ st.alive c = true ∧
      st.cont c = .ptr b := by
    intro c b hk ha hc
    rcases enter_cases hw ha with h | ⟨i, t, hi, rfl⟩
    · have := enter_old (D := D) (vals := vals) h
      exact ⟨h, this.2.2.symm, this.1 ▸ ha, this.2.1 ▸ hc⟩
    · have hn := enter_new (st := st) (vals := vals) hi
      have : D[i]? = some .own ∨ D[i]? = some .vown := by
        rw [hn.2.2] at hk
        rw [hi]; cases t <;> simp_all [Ty.kind]
      rw [hn.2.1, hv i this] at hc; cases hc
  have o0 := enter_old (st := st) (D := D) (vals := vals) (b := 0) (by omega)
  have o1 := enter_old (st := st) (D := D) (vals := vals) (b := 1) (by omega)
  refine ⟨fun b h => ?_, Nat.le_trans h2 (Nat.le_add_right _ _), ?_, ?_, fun c b hk ha hc => ?_,
    fun c b hk ha hc => ?_, fun c c' b hk hk' ha ha' hc hc' => ?_⟩
  · rcases enter_cases hw h with h | ⟨i, t, hi, rfl⟩
    · exact Nat.lt_of_lt_of_le h (Nat.le_add_right _ _)
    · exact Nat.add_lt_add_left (List.getElem?_eq_some_iff.mp hi).1 _
  · rw [o0.1, o0.2.2]; exact hw.g0
  · rw [o1.2.1, o1.2.2]; exact hw.g1
  · obtain ⟨_, h1, h2, h3⟩ := key c b (.inl hk) ha hc
    have := hw.own c b (h1.trans hk) h2 h3
    have ho := enter_old (D := D) (vals := vals) (hw.lt b this.1)
    rw [ho.1, ho.2.2]; exact this
  · obtain ⟨_, h1, h2, h3⟩ := key c b (.inr hk) ha hc
    have := hw.view c b (h1.trans hk) h2 h3
    have ho := enter_old (D := D) (vals := vals) (hw.lt b this.1)
    rw [ho.1, ho.2.2]; exact this
  · obtain ⟨_, h1, h2, h3⟩ := key c b hk ha hc
    obtain ⟨_, h1', h2', h3'⟩ := key c' b hk' ha' hc'
    exact hw.uniq c c' b (by rw [h1]; exact hk) (by rw [h1']; exact hk') h2 h2' h3 h3'

/-- The invariant on entry to a body. -/
theorem inv_enter {X : Ctx} (hXD : X.D = D) (hXf : X.fp = st.next) (hw : WF st)
    (hv : ∀ (i : Nat) (t : Ty), D[i]? = some t → t ≠ .bor → t ≠ .out → t ≠ .sh → t ≠ .ex → vals[i]? = none)
    (hp : ∀ i : Nat, D[i]? = some .out ∨ D[i]? = some .sh ∨ D[i]? = some .ex →
      vals[i]? = some (.ptr (X.a i)) ∧ st.alive (X.a i) = true)
    (hobj : ∀ i : Nat, D[i]? = some .sh ∨ D[i]? = some .ex → st.kind (X.a i) = .ocell ∧ X.a i ≠ 0)
    (hout : ∀ i : Nat, D[i]? = some .out → st.kind (X.a i) = .bvar ∨ st.kind (X.a i) = .hview)
    (hap : ∀ v w : Nat, D[v]? = some .ex → D[w]? = some .sh ∨ D[w]? = some .ex → v ≠ w → X.a v ≠ X.a w)
    (hq : ∀ i b : Nat, (D[i]? = some .bor ∧ vals[i]? = some (.ptr b)) ∨
        (D[i]? = some .out ∧ st.cont (X.a i) = .ptr b) →
      X.Q i b ∧ st.alive b = true ∧ (st.kind b = .hint ∨ st.kind b = .lint) ∧
      ∀ c, MC X c → st.cont c ≠ .ptr b) :
    Inv X (Abs.entry D) (enter st D vals) := by
  subst hXD
  have h2 := hw.two
  have hmc : ∀ c, MC X c → c < st.next := by
    rintro c (rfl | ⟨v, h, rfl⟩)
    · omega
    · exact hw.lt _ (hp v (.inr (.inr h))).2
  refine ⟨wf_enter hw fun i h => ?_, by omega, by rw [hXf]; exact Nat.le_add_right _ _,
    fun i t h => ?_, fun i h => ?_, fun i h => ?_, fun i h => ?_, hap, fun v c hc L b hL hb => ?_,
    fun q t L h1 _ h3 => ?_, fun i b h hb => ?_⟩
  · rcases h with h | h <;> exact hv i _ h (by simp) (by simp) (by simp) (by simp)
  · rw [hXf]; have := enter_new (st := st) (vals := vals) h; exact ⟨this.1, this.2.2⟩
  · have hp' := hp i h
    have hlt := hw.lt _ hp'.2
    have ho := enter_old (D := X.D) (vals := vals) hlt
    have hd : ∃ t, X.D[i]? = some t := by rcases h with h | h | h <;> exact ⟨_, h⟩
    obtain ⟨t, hd⟩ := hd
    have hn := enter_new (st := st) (vals := vals) hd
    rw [hXf]
    exact ⟨by rw [hn.2.1, hp'.1]; rfl, ho.1.trans hp'.2, hlt⟩
  · have ho := enter_old (D := X.D) (vals := vals) (hw.lt _ (hp i (.inr h)).2)
    rw [ho.2.2]; exact hobj i h
  · have ho := enter_old (D := X.D) (vals := vals) (hw.lt _ (hp i (.inl h)).2)
    rw [ho.2.2]; exact hout i h
  · have hcase : ((X.D[v]? = some .bor ∧ vals[v]? = some (.ptr b)) ∨
        (X.D[v]? = some .out ∧ st.cont (X.a v) = .ptr b)) := by
      rcases hc with ⟨h, rfl⟩ | ⟨h, h'⟩ | ⟨h, rfl⟩
      · rw [hXf, (enter_new h).2.1] at hb
        refine .inl ⟨h, ?_⟩
        cases hx : vals[v]? with
        | none => rw [hx] at hb; cases hb
        | some x => rw [hx] at hb; simp at hb; rw [hb]
      · rw [hXf, (enter_new h).2.1, hv v _ h (by simp) (by simp) (by simp) (by simp)] at h'
        cases h'
      · rw [(enter_old (hw.lt _ (hp v (.inl h)).2)).2.1] at hb
        exact .inr ⟨h, hb⟩
    obtain ⟨hQ, ha, hk, hu⟩ := hq v b hcase
    have ho := enter_old (D := X.D) (vals := vals) (hw.lt b ha)
    have hL' : L = [.par v] := by
      rcases hcase with h | h <;> simp only [Abs.entry, h.1, Option.some.injEq] at hL <;>
        exact hL.symm
    subst hL'
    refine ⟨ho.1.trans ha, by rw [ho.2.2]; exact hk, .par v, List.mem_singleton.mpr rfl, hQ,
      by rw [hXf]; exact hw.lt b ha, fun c hc => ?_⟩
    rw [(enter_old (hmc c hc)).2.1]; exact hu c hc
  · simp [Abs.entry, h1] at h3
  · exfalso
    have hd : ∃ t, X.D[i]? = some t ∧ t ≠ .bor ∧ t ≠ .out ∧ t ≠ .sh ∧ t ≠ .ex := by
      rcases h with h | h <;> exact ⟨_, h, by simp, by simp, by simp, by simp⟩
    obtain ⟨t, hd, h1, h2, h3, h4⟩ := hd
    rw [hXf, (enter_new hd).2.1, hv i t hd h1 h2 h3 h4] at hb; cases hb

theorem leave_out {st : St} {fp W b : Nat} (h : b < fp ∨ fp + W ≤ b) :
    (leave st fp W).alive b = st.alive b := by
  have : ¬ (fp ≤ b ∧ b < fp + W) := by omega
  simp only [leave, ite_eq_right this]

theorem leave_alive {st : St} {fp W b : Nat} (h : (leave st fp W).alive b = true) :
    st.alive b = true := by
  simp only [leave] at h
  split at h
  · cases h
  · exact h

/-- Heap blocks are not variables. -/
theorem leave_heap {X : Ctx} {A : Abs} (hI : Inv X A st) {b : Nat} (ha : st.alive b = true)
    (hk : st.kind b = .hint ∨ st.kind b = .hview) :
    (leave st X.fp X.D.length).alive b = true := by
  refine (leave_out ?_).trans ha
  by_cases h : X.fp ≤ b ∧ b < X.fp + X.D.length
  · exfalso
    have hd : X.D[b - X.fp]? = some X.D[b - X.fp] := List.getElem?_eq_getElem (by omega)
    have := (hI.fr _ _ hd).2
    rw [show X.fp + (b - X.fp) = b by omega] at this
    rw [this] at hk
    generalize X.D[b - X.fp] = t at hk
    cases t <;> simp [Ty.kind] at hk
  · omega

theorem wf_leave {X : Ctx} {A : Abs} (hI : Inv X A st) : WF (leave st X.fp X.D.length) := by
  have hw := hI.wf
  have h2 := hI.fp2
  refine ⟨fun b h => hw.lt b (leave_alive h), hw.two,
    ⟨by rw [leave_out (.inl (by omega))]; exact hw.g0.1, hw.g0.2⟩, hw.g1, fun c b hk ha hc => ?_,
    fun c b hk ha hc => ?_, fun c c' b hk hk' ha ha' hc hc' => ?_⟩
  · have := hw.own c b hk (leave_alive ha) hc
    exact ⟨leave_heap hI this.1 (.inl this.2), this.2⟩
  · have := hw.view c b hk (leave_alive ha) hc
    exact ⟨leave_heap hI this.1 (.inr this.2), this.2⟩
  · exact hw.uniq c c' b hk hk' (leave_alive ha) (leave_alive ha') hc hc'

end

/-! ## Calls -/

def Post (X : Ctx) (A : Abs) (st : St) : Out → Prop
  | .ok st' => Inv X A st' ∧ Eff (MC X) (WC X) X.fp st st'
  | .ub _ => False
  | .trap | .outOfFuel => True

theorem killAll_some {vs : List Nat} {A : Abs} {v : Nat} {L : List Loan}
    (h : killAll vs A v = some L) : A v = some L ∧ ∀ w ∈ vs, Loan.own w ∉ L := by
  induction vs with
  | nil => exact ⟨h, by simp⟩
  | cons w vs ih =>
    obtain ⟨h1, h2⟩ := kill_some (A := killAll vs A) h
    obtain ⟨h3, h4⟩ := ih h1
    refine ⟨h3, fun u hu => ?_⟩
    rcases List.mem_cons.mp hu with rfl | hu
    · exact h2
    · exact h4 u hu

theorem mem_exArgs {as : Args} {v : Nat} :
    v ∈ exArgs as ↔ ∃ (i : Nat) (ne : Bool), as[i]? = some ((.ex, ne), v) := by
  unfold exArgs
  rw [List.mem_filterMap]
  constructor
  · rintro ⟨⟨⟨t, ne⟩, w⟩, hm, h⟩
    obtain ⟨i, hi⟩ := List.mem_iff_getElem?.mp hm
    by_cases ht : t = .ex
    · subst ht; simp at h; subst h; exact ⟨i, ne, hi⟩
    · simp [ht] at h
  · rintro ⟨i, ne, h⟩; exact ⟨_, List.mem_iff_getElem?.mpr ⟨i, h⟩, by simp⟩

theorem mem_outArgs {as : Args} {v : Nat} :
    v ∈ outArgs as ↔ ∃ (i : Nat) (ne : Bool), as[i]? = some ((.out, ne), v) := by
  unfold outArgs
  rw [List.mem_filterMap]
  constructor
  · rintro ⟨⟨⟨t, ne⟩, w⟩, hm, h⟩
    obtain ⟨i, hi⟩ := List.mem_iff_getElem?.mp hm
    by_cases ht : t = .out
    · subst ht; simp at h; subst h; exact ⟨i, ne, hi⟩
    · simp [ht] at h
  · rintro ⟨i, ne, h⟩; exact ⟨_, List.mem_iff_getElem?.mpr ⟨i, h⟩, by simp⟩

/-- Two object arguments in different places with the same variable count twice. -/
theorem count_objs : ∀ (as : Args) (i j : Nat) (p q : (Ty × Bool) × Nat), i ≠ j →
    as[i]? = some p → as[j]? = some q → (p.1.1 = .sh ∨ p.1.1 = .ex) →
    (q.1.1 = .sh ∨ q.1.1 = .ex) → p.2 = q.2 → 2 ≤ (objArgs as).count p.2
  | [], i, _, _, _, _, h, _, _, _, _ => by simp at h
  | a :: as, i, j, p, q, hij, hi, hj, hp, hq, he => by
    have one : ∀ (as : Args) (k : Nat) (r : (Ty × Bool) × Nat), as[k]? = some r →
        (r.1.1 = .sh ∨ r.1.1 = .ex) → 1 ≤ (objArgs as).count r.2 := by
      intro as k r h hr
      rw [Nat.succ_le_iff, List.count_pos_iff]
      exact List.mem_filterMap.mpr ⟨r, List.mem_iff_getElem?.mpr ⟨k, h⟩, by simp [hr]⟩
    have cons : ∀ (r : (Ty × Bool) × Nat), (r.1.1 = .sh ∨ r.1.1 = .ex) →
        objArgs (r :: as) = r.2 :: objArgs as := by intro r hr; simp [objArgs, hr]
    cases i with
    | zero =>
      cases j with
      | zero => exact absurd rfl hij
      | succ j =>
        simp at hi hj
        subst hi
        have := one as j q hj hq
        rw [cons a hp, List.count_cons_self, he]
        omega
    | succ i =>
      cases j with
      | zero =>
        simp at hi hj
        subst hj
        have := one as i p hi hp
        rw [cons a hq, he, List.count_cons_self, ← he]
        omega
      | succ j =>
        simp at hi hj
        have := count_objs as i j p q (by omega) hi hj hp hq he
        refine Nat.le_trans this ?_
        unfold objArgs
        rw [List.filterMap_cons]
        split
        · exact Nat.le_refl _
        · exact List.count_le_count_cons

/-- What the call check establishes, under the full rules. -/
structure CallOK (D : List Ty) (A : Abs) (F : Fn) (args : List Nat) (A2 : Abs) (src : List Loan) :
    Prop where
  len : args.length = F.params.length
  typed : ∀ p ∈ F.params.zip args, argOK D p.1.1 p.2 = true
  live : ∀ p ∈ F.params.zip args,
    argLive (killAll (exArgs (F.params.zip args)) (kill .glob A)) p = true
  once : ∀ v ∈ exArgs (F.params.zip args), (objArgs (F.params.zip args)).count v = 1
  srcEq : src = (F.params.zip args).flatMap
    (argSrc Rules.full (killAll (exArgs (F.params.zip args)) (kill .glob A)))
  flow : ∀ v L, A2 v = some L → ∃ M,
    killAll (exArgs (F.params.zip args)) (kill .glob A) v = some M ∧
    (∀ l ∈ M, l ∈ L) ∧
    (v ∈ outArgs (F.params.zip args) ∨
      ((∃ w ∈ outArgs (F.params.zip args), D[w]? = some .out) ∧ D[v]? = some .out) →
        ∀ l ∈ src, l ∈ L)

theorem chkCall_ok {D : List Ty} {A A' : Abs} {F : Fn} {args : List Nat} {ret : Option Nat}
    (h : chkCall Rules.full D A F args ret = some A') :
    ∃ A2 src, CallOK D A F args A2 src ∧
      match ret with
      | none => A' = A2
      | some y => D[y]? = some .bor ∧ F.ret.isSome = true ∧ A' = upd A2 y (some src) := by
  simp only [chkCall, Rules.full, ite_true] at h
  split at h
  · rename_i hc
    obtain ⟨h1, h2, h3, h4⟩ := hc
    rw [List.all_eq_true] at h2 h3
    refine ⟨fun v => if (true && (decide (v ∈ outArgs (F.params.zip args)) ||
          (true && (outArgs (F.params.zip args)).any (fun v => decide (D[v]? = some .out)) &&
            decide (D[v]? = some .out)))) = true then
        Option.map ((F.params.zip args).flatMap
          (argSrc Rules.full (killAll (exArgs (F.params.zip args)) (kill .glob A))) ++ ·)
          (killAll (exArgs (F.params.zip args)) (kill .glob A) v)
        else killAll (exArgs (F.params.zip args)) (kill .glob A) v, _,
      ⟨h1, h2, h3, fun v hv => by simpa using List.all_eq_true.mp (h4 trivial) v hv, rfl, ?_⟩, ?_⟩
    · intro v L hL
      split at hL
      · rw [Option.map_eq_some_iff] at hL
        obtain ⟨M, hM, rfl⟩ := hL
        exact ⟨M, hM, fun l hl => List.mem_append_right _ hl,
          fun _ l hl => List.mem_append_left _ hl⟩
      · rename_i hn
        refine ⟨L, hL, fun _ h => h, fun hv => (hn ?_).elim⟩
        rcases hv with hv | ⟨⟨w, hw, hw'⟩, hv⟩
        · simp [hv]
        · simp only [Bool.true_and, Bool.or_eq_true, Bool.and_eq_true, decide_eq_true_eq,
            List.any_eq_true]
          exact .inr ⟨⟨w, hw, hw'⟩, hv⟩
    · cases ret with
      | none => simp only [Option.some.injEq] at h; exact h.symm
      | some y =>
        simp only at h
        split at h
        · rename_i hy
          simp only [Option.some.injEq] at h
          exact ⟨hy.1, hy.2, h.symm⟩
        · cases h
  · cases h

theorem zip_spec {D : List Ty} {fp : Nat} {st : St} {F : Fn} {args : List Nat} {i : Nat}
    {p : Ty × Bool} {v : Nat} (h : (F.params.zip args)[i]? = some (p, v)) :
    F.tys[i]? = some p.1 ∧ F.params[i]? = some p ∧
    (List.zipWith (fun p v => argVal D fp st p.1 v) F.params args)[i]? =
      some (argVal D fp st p.1 v) := by
  rw [List.getElem?_zip_eq_some] at h
  have hi : i < F.params.length := (List.getElem?_eq_some_iff.mp h.1).1
  refine ⟨?_, h.1, ?_⟩
  · unfold Fn.tys
    rw [List.getElem?_append_left (by simpa using hi)]
    simp [h.1]
  · rw [List.getElem?_zipWith, h.1, h.2]

theorem zip_of {F : Fn} {args : List Nat} (hl : args.length = F.params.length) {i : Nat} {t : Ty}
    (ht : F.tys[i]? = some t) (hi : i < F.params.length) :
    ∃ ne v, (F.params.zip args)[i]? = some ((t, ne), v) ∧ F.noesc i = ne := by
  have h1 := List.getElem?_eq_getElem hi
  have h2 := List.getElem?_eq_getElem (l := args) (i := i) (by omega)
  unfold Fn.tys at ht
  rw [List.getElem?_append_left (by simpa using hi)] at ht
  simp only [List.getElem?_map, h1, Option.map_some, Option.some.injEq] at ht
  subst ht
  exact ⟨F.params[i].2, args[i], by rw [List.getElem?_zip_eq_some]; exact ⟨h1, h2⟩,
    by simp [Fn.noesc, h1]⟩

theorem argOK_out {D : List Ty} {v : Nat} (h : argOK D .out v = true) :
    D[v]? = some .bor ∨ D[v]? = some .vown ∨ D[v]? = some .out := by
  unfold argOK at h
  revert h
  generalize D[v]? = o
  intro h
  rcases o with _ | t
  · cases h
  · cases t <;> simp at h ⊢

theorem argOK_bor {D : List Ty} {v : Nat} (h : argOK D .bor v = true) : D[v]? = some .bor := by
  unfold argOK at h
  revert h
  generalize D[v]? = o
  intro h
  rcases o with _ | t
  · cases h
  · cases t <;> simp at h ⊢

theorem argOK_obj {D : List Ty} {v : Nat} {t : Ty} (ht : t = .sh ∨ t = .ex)
    (h : argOK D t v = true) : D[v]? = some .own ∨ ((D[v]? = some .sh ∨ D[v]? = some .ex) ∧
      (t = .ex → D[v]? = some .ex)) := by
  unfold argOK at h
  revert h
  generalize D[v]? = o
  intro h
  rcases o with _ | s
  · rcases ht with rfl | rfl <;> cases h
  · rcases ht with rfl | rfl <;> cases s <;> simp at h ⊢

set_option maxHeartbeats 1000000 in
/-- A call, from the callee's body check alone: the body is run with the
callee's own context, and only its invariant at exit is used. -/
theorem call_ok {P : List Fn} {X : Ctx} {A A' : Abs} {st : St} {f : Nat} {F : Fn}
    {args : List Nat} {ret : Option Nat} {n : Nat} (hI : Inv X A st) (hPf : P[f]? = some F)
    (hF : chkFn Rules.full P F = true)
    (hbody : ∀ X A A' st, X.D = F.tys → Inv X A st → chk Rules.full P X.D A F.body = some A' →
      Post X A' st (exec P n X.D X.fp st F.body))
    (hc : chkCall Rules.full X.D A F args ret = some A') :
    Post X A' st (exec P (n + 1) X.D X.fp st (.call f args ret)) := by
  obtain ⟨A2, src, hC, hret⟩ := chkCall_ok hc
  simp only [exec, hPf]
  generalize hvals : List.zipWith (fun p v => argVal X.D X.fp st p.1 v) F.params args = vals
  split
  · trivial
  rename_i hnull
  -- The callee's signature, seen from the body check.
  simp only [chkFn, Bool.and_eq_true] at hF
  obtain ⟨⟨hpt, hlt⟩, hF⟩ := hF
  rw [List.all_eq_true] at hpt hlt
  split at hF
  · cases hF
  rename_i A1 hA1
  simp only [Bool.and_eq_true] at hF
  obtain ⟨hexit, hexitR⟩ := hF
  rw [List.all_eq_true] at hexit
  have hvlen : vals.length = F.params.length := by
    rw [← hvals, List.length_zipWith, hC.len, Nat.min_self]
  have hparam : ∀ {i : Nat} {t : Ty}, F.tys[i]? = some t → localTy t = false → i < F.params.length := by
    intro i t ht hl
    refine Classical.byContradiction fun hge => ?_
    unfold Fn.tys at ht
    rw [List.getElem?_append_right (by simpa using hge)] at ht
    have := hlt t (List.mem_of_getElem? ht)
    rw [hl] at this; cases this
  have hlocal : ∀ {i : Nat} {t : Ty}, F.tys[i]? = some t → paramTy t = false → vals[i]? = none := by
    intro i t ht hl
    refine List.getElem?_eq_none (Nat.le_of_not_lt fun hi => ?_)
    obtain ⟨ne, v, hz, _⟩ := zip_of hC.len ht (by omega)
    have := hpt _ (List.mem_of_getElem? (zip_spec (D := X.D) (fp := X.fp) (st := st) hz).2.1)
    rw [hl] at this; cases this
  have hnn : ∀ {i : Nat} {p : Ty × Bool} {v : Nat}, (F.params.zip args)[i]? = some (p, v) →
      p.1 ≠ .bor → argVal X.D X.fp st p.1 v ≠ .null := by
    intro i p v hz hb h0
    have hzs := zip_spec (D := X.D) (fp := X.fp) (st := st) hz
    rw [hvals] at hzs
    apply hnull
    rw [List.any_eq_true]
    refine ⟨true, List.mem_of_getElem? (i := i) ?_, rfl⟩
    rw [List.getElem?_zipWith, hzs.2.1, hzs.2.2, h0]
    simp [hb]
  have hmem : ∀ {i : Nat} {p : (Ty × Bool) × Nat}, (F.params.zip args)[i]? = some p →
      p ∈ F.params.zip args := fun h => List.mem_of_getElem? h
  -- What the arguments are.
  have hbor : ∀ {i : Nat} {ne : Bool} {v : Nat}, (F.params.zip args)[i]? = some ((.bor, ne), v) →
      X.D[v]? = some .bor ∧ vals[i]? = some (st.cont (X.fp + v)) := by
    intro i ne v hz
    have hzs := zip_spec (D := X.D) (fp := X.fp) (st := st) hz
    rw [hvals] at hzs
    have ht := argOK_bor (hC.typed _ (hmem hz))
    exact ⟨ht, by rw [hzs.2.2]; simp [argVal]⟩
  have hout : ∀ {i : Nat} {ne : Bool} {v : Nat}, (F.params.zip args)[i]? = some ((.out, ne), v) →
      ∃ c, vals[i]? = some (.ptr c) ∧ CCell X st v c := by
    intro i ne v hz
    have hzs := zip_spec (D := X.D) (fp := X.fp) (st := st) hz
    rw [hvals] at hzs
    have ht := hC.typed _ (hmem hz)
    have hn := hnn hz (by simp)
    have hv := hzs.2.2
    simp only at hv hn
    rcases argOK_out ht with h | h | h
    · refine ⟨X.fp + v, ?_, .inl ⟨h, rfl⟩⟩
      rw [hv]; simp [argVal, h]
    · have he : argVal X.D X.fp st .out v = st.cont (X.fp + v) := by simp [argVal, h]
      rw [he] at hv hn
      cases hcc : st.cont (X.fp + v) with
      | null => exact absurd hcc hn
      | ptr c => exact ⟨c, by rw [hv, hcc], .inr (.inl ⟨h, hcc⟩)⟩
    · have he : argVal X.D X.fp st .out v = st.cont (X.fp + v) := by simp [argVal, h]
      rw [he, (hI.tgt v (.inl h)).1] at hv
      exact ⟨_, hv, .inr (.inr ⟨h, rfl⟩)⟩
  have hobj : ∀ {i : Nat} {t : Ty} {ne : Bool} {v : Nat},
      (F.params.zip args)[i]? = some ((t, ne), v) → (t = .sh ∨ t = .ex) →
      ∃ c, vals[i]? = some (.ptr c) ∧ OCell X v c ∧
        (t = .ex → X.D[v]? = some .own ∨ X.D[v]? = some .ex) := by
    intro i t ne v hz hte
    have hzs := zip_spec (D := X.D) (fp := X.fp) (st := st) hz
    rw [hvals] at hzs
    have ht := hC.typed _ (hmem hz)
    have hv := hzs.2.2
    simp only at ht hv
    have hd := argOK_obj hte ht
    rcases hd with hd | ⟨hd, hx⟩
    · refine ⟨X.fp + v, ?_, .inl ⟨hd, rfl⟩, fun _ => .inl hd⟩
      rw [hv]; rcases hte with rfl | rfl <;> simp [argVal, hd]
    · refine ⟨X.a v, ?_, .inr ⟨hd, rfl⟩, fun h => .inr (hx h)⟩
      rw [hv, ← (hI.tgt v (.inr hd)).1]
      rcases hte with rfl | rfl <;> rcases hd with hd | hd <;> simp [argVal, hd]
  -- The callee's frame.
  let a' : Nat → Nat := fun i => match vals[i]? with | some (.ptr c) => c | _ => 0
  have ha' : ∀ {i c : Nat}, vals[i]? = some (.ptr c) → a' i = c := by
    intro i c h; simp only [a', h]
  let Y : Ctx := ⟨F.tys, st.next, a', fun i b =>
    (F.tys[i]? = some .bor ∧ vals[i]? = some (.ptr b)) ∨
      (F.tys[i]? = some .out ∧ st.cont (a' i) = .ptr b)⟩
  let as := F.params.zip args
  let M0 : Nat → Prop := fun c => c = 0 ∨ ∃ v ∈ exArgs as, OCell X v c
  let W0 : Nat → Prop := fun c => ∃ v ∈ outArgs as, CCell X st v c
  let B := killAll (exArgs as) (kill .glob A)
  have hexown : ∀ v ∈ exArgs as, X.D[v]? = some .own ∨ X.D[v]? = some .ex := by
    intro v hv
    obtain ⟨i, ne, hz⟩ := mem_exArgs.mp hv
    obtain ⟨_, _, _, h⟩ := hobj hz (.inr rfl)
    exact h rfl
  have hM : MOK X M0 := by
    rintro c (rfl | ⟨v, hv, ho⟩)
    · exact .inl (.inl rfl)
    · rcases hexown v hv with h | h
      · rcases ho with ⟨_, ho⟩ | ⟨h', _⟩
        · exact .inr ⟨v, .inl h, ho⟩
        · rw [h] at h'; simp at h'
      · rcases ho with ⟨h', _⟩ | ⟨_, ho⟩
        · rw [h] at h'; cases h'
        · exact .inl (.inr ⟨v, h, ho⟩)
  have hck : ∀ {v c : Nat}, CCell X st v c → (st.kind c = .bvar ∨ st.kind c = .hview) ∧
      st.alive c = true ∧ (WC X c ∨ X.fp ≤ c) := by
    intro v c hc
    rcases hc with ⟨h, rfl⟩ | ⟨h, h'⟩ | ⟨h, rfl⟩
    · exact ⟨.inl (hI.fr v _ h).2, (hI.fr v _ h).1, .inr (Nat.le_add_right _ _)⟩
    · have := hI.wf.view _ _ (hI.fr v _ h).2 (hI.fr v _ h).1 h'
      exact ⟨.inr this.2, this.1, .inr (hI.fresh v _ (.inr h) h')⟩
    · exact ⟨hI.outk v h, (hI.tgt v (.inl h)).2.1, .inl ⟨v, h, rfl⟩⟩
  have hW : WOK X st W0 := by
    rintro c ⟨v, _, hcc⟩
    have := hck hcc
    exact ⟨.inl this.1, this.2.2, this.2.1⟩
  have hnk : ∀ {v : Nat} {L : List Loan}, B v = some L → NoKill X M0 L ∧ A v = some L := by
    intro v L hL
    obtain ⟨h1, h2⟩ := killAll_some hL
    obtain ⟨h3, h4⟩ := kill_some h1
    refine ⟨fun l hl => ?_, h3⟩
    cases l with
    | own w =>
      rintro c ho (rfl | ⟨u, hu, ho'⟩)
      · exact hI.ocell_ne0 ho
      · have := hI.ocell_inj ho' ho (hexown u hu)
        subst this; exact h2 u hu hl
    | glob =>
      rintro (_ | ⟨u, _, ho⟩)
      · exact h4 hl
      · exact hI.ocell_ne0 ho
    | _ => trivial
  have hlive : ∀ {i : Nat} {p : Ty × Bool} {v : Nat}, as[i]? = some (p, v) →
      (p.1 = .bor ∨ p.1 = .out) → ∃ L, B v = some L := by
    intro i p v hz hp
    have := hC.live _ (hmem hz)
    simp only [argLive, hp, ite_true] at this
    exact Option.isSome_iff_exists.mp this
  have hMC : ∀ c, MC Y c → M0 c := by
    rintro c (rfl | ⟨i, hi, rfl⟩)
    · exact .inl rfl
    · obtain ⟨ne, v, hz, _⟩ := zip_of hC.len hi (hparam hi rfl)
      obtain ⟨c, hv, ho, _⟩ := hobj hz (.inr rfl)
      exact .inr ⟨v, mem_exArgs.mpr ⟨i, ne, hz⟩, by show OCell X v (a' i); rw [ha' hv]; exact ho⟩
  have hWC : ∀ c, WC Y c → W0 c := by
    rintro c ⟨i, hi, rfl⟩
    obtain ⟨ne, v, hz, _⟩ := zip_of hC.len hi (hparam hi rfl)
    obtain ⟨c, hv, ho⟩ := hout hz
    exact ⟨v, mem_outArgs.mpr ⟨i, ne, hz⟩, by show CCell X st v (a' i); rw [ha' hv]; exact ho⟩
  -- What parameter i brought in is a borrow the caller had.
  have hQ : ∀ i b, Y.Q i b → ∃ ne v t L, as[i]? = some ((t, ne), v) ∧ F.noesc i = ne ∧
      F.tys[i]? = some t ∧ (t = .bor ∨ t = .out) ∧ B v = some L ∧ Good X st L b := by
    rintro i b (⟨ht, hv⟩ | ⟨ht, hv⟩)
    · have hi : i < F.params.length := by
        have := (List.getElem?_eq_some_iff.mp hv).1; omega
      obtain ⟨ne, v, hz, hne⟩ := zip_of hC.len ht hi
      obtain ⟨hd, hv'⟩ := hbor hz
      obtain ⟨L, hL⟩ := hlive hz (.inl rfl)
      rw [hv'] at hv
      exact ⟨ne, v, _, L, hz, hne, ht, .inl rfl, hL,
        hI.val v _ (.inl ⟨hd, rfl⟩) L b (hnk hL).2 (Option.some.inj hv)⟩
    · obtain ⟨ne, v, hz, hne⟩ := zip_of hC.len ht (hparam ht rfl)
      obtain ⟨c, hv', hcc⟩ := hout hz
      obtain ⟨L, hL⟩ := hlive hz (.inr rfl)
      rw [ha' hv'] at hv
      exact ⟨ne, v, _, L, hz, hne, ht, .inr rfl, hL, hI.val v c hcc L b (hnk hL).2 hv⟩
  have hInvY : Inv Y (Abs.entry F.tys) (enter st F.tys vals) := by
    refine inv_enter rfl rfl hI.wf (fun i t ht h1 h2 h3 h4 => ?_) (fun i hi => ?_) (fun i hi => ?_)
      (fun i hi => ?_) (fun i j hi hj hij => ?_) (fun i b h => ?_)
    · exact hlocal ht (by cases t <;> simp_all [paramTy])
    · rcases hi with hi | hi
      · obtain ⟨ne, v, hz, _⟩ := zip_of hC.len hi (hparam hi rfl)
        obtain ⟨c, hv, hcc⟩ := hout hz
        show vals[i]? = some (.ptr (a' i)) ∧ st.alive (a' i) = true
        rw [ha' hv]; exact ⟨hv, (hck hcc).2.1⟩
      · have : ∃ t, F.tys[i]? = some t ∧ localTy t = false ∧ (t = .sh ∨ t = .ex) := by
          rcases hi with hi | hi <;> exact ⟨_, hi, rfl, by simp⟩
        obtain ⟨t, hi, hl, hte⟩ := this
        obtain ⟨ne, v, hz, _⟩ := zip_of hC.len hi (hparam hi hl)
        obtain ⟨c, hv, ho, _⟩ := hobj hz hte
        show vals[i]? = some (.ptr (a' i)) ∧ st.alive (a' i) = true
        rw [ha' hv]; exact ⟨hv, (hI.ocell ho).2⟩
    · have : ∃ t, F.tys[i]? = some t ∧ localTy t = false ∧ (t = .sh ∨ t = .ex) := by
        rcases hi with hi | hi <;> exact ⟨_, hi, rfl, by simp⟩
      obtain ⟨t, hi, hl, hte⟩ := this
      obtain ⟨ne, v, hz, _⟩ := zip_of hC.len hi (hparam hi hl)
      obtain ⟨c, hv, ho, _⟩ := hobj hz hte
      show st.kind (a' i) = .ocell ∧ a' i ≠ 0
      rw [ha' hv]
      exact ⟨(hI.ocell ho).1, by rintro rfl; exact hI.ocell_ne0 ho⟩
    · obtain ⟨ne, v, hz, _⟩ := zip_of hC.len hi (hparam hi rfl)
      obtain ⟨c, hv, hcc⟩ := hout hz
      show st.kind (a' i) = .bvar ∨ st.kind (a' i) = .hview
      rw [ha' hv]; exact (hck hcc).1
    · have : ∃ t, F.tys[j]? = some t ∧ localTy t = false ∧ (t = .sh ∨ t = .ex) := by
        rcases hj with hj | hj <;> exact ⟨_, hj, rfl, by simp⟩
      obtain ⟨t, hj, hl, hte⟩ := this
      obtain ⟨ne, v, hz, _⟩ := zip_of hC.len hi (hparam hi rfl)
      obtain ⟨ne', w, hz', _⟩ := zip_of hC.len hj (hparam hj hl)
      obtain ⟨c, hv, ho, hx⟩ := hobj hz (.inr rfl)
      obtain ⟨c', hv', ho', _⟩ := hobj hz' hte
      show a' i ≠ a' j
      rw [ha' hv, ha' hv']
      rintro rfl
      have hvw := hI.ocell_inj ho ho' (hx rfl)
      have h2 := count_objs _ i j _ _ hij hz hz' (.inr rfl) hte hvw
      have h1 := hC.once v (mem_exArgs.mpr ⟨i, ne, hz⟩)
      simp only at h2
      omega
    · have hq : Y.Q i b := h
      obtain ⟨ne, v, t, L, _, _, _, _, hL, hG⟩ := hQ i b hq
      exact ⟨hq, hG.1, hG.2.1, fun c hc => good_unowned hI hM (hnk hL).1 hG c (hMC c hc)⟩
  have hpost := hbody Y _ A1 _ rfl hInvY hA1
  change Post X A' st ((exec P n F.tys st.next (enter st F.tys vals) F.body).then _)
  have hpost : Post Y A1 (enter st F.tys vals)
    (exec P n F.tys st.next (enter st F.tys vals) F.body) := hpost
  revert hpost
  cases exec P n F.tys st.next (enter st F.tys vals) F.body with
  | trap => intro _; trivial
  | outOfFuel => intro _; trivial
  | ub k => exact fun h => h
  | ok st1 =>
  rintro ⟨hI1, hE1⟩
  simp only [Out.then]
  generalize hst2 : leave st1 st.next F.tys.length = st2
  have hwf2 : WF st2 := by rw [← hst2]; exact wf_leave hI1
  have hc2 : st2.cont = st1.cont := by rw [← hst2]; rfl
  have hk2 : st2.kind = st1.kind := by rw [← hst2]; rfl
  have hE : Eff M0 W0 st.next st st2 := by
    have hold : ∀ {b}, b < st.next → _ := fun {b} hb =>
      enter_old (st := st) (D := F.tys) (vals := vals) (b := b) hb
    have hM0lt : ∀ {c}, M0 c → c < st.next := fun h => hI.wf.lt _ (hI.mok hM h).2
    refine ⟨?_, fun b hb => ?_, fun b hb ha => ?_, fun b hb h1 h2 => ?_, fun c b hc hb h => ?_⟩
    · have : st.next + F.tys.length ≤ st1.next := hE1.nx
      rw [← hst2]; exact Nat.le_trans (Nat.le_add_right _ _) this
    · rw [hk2, hE1.kind b hb, (hold hb).2.2]
    · rcases hE1.alive b hb ((hold hb).1.trans ha) with h | ⟨c, hc, h⟩
      · left; rw [← hst2]; exact (leave_out (.inl hb)).trans h
      · have hm := hMC c hc
        rw [(hold (hM0lt hm)).2.1] at h
        exact .inr ⟨c, hm, h⟩
    · rw [hc2, hE1.cont b hb (fun h => h1 (hMC b h)) (fun h => h2 (hWC b h)), (hold hb).2.1]
    · rw [hc2] at h
      by_cases hm : MC Y c
      · have := hE1.mt c b hm hb h
        rwa [(hold (hM0lt hc)).2.1] at this
      · rw [hE1.cont c (hM0lt hc) hm (fun hw => ?_), (hold (hM0lt hc)).2.1] at h
        · exact h
        · obtain ⟨v, _, hcc⟩ := hWC c hw
          have h1 := (hck hcc).1
          have h2 := (hI.mok hM hc).1
          rcases h1 with h1 | h1 <;> rw [h1] at h2 <;> simp at h2
  have hsrc : ∀ {i : Nat} {p : (Ty × Bool) × Nat}, as[i]? = some p →
      ∀ l ∈ argSrc Rules.full B p, l ∈ src := by
    intro i p hz l hl
    rw [hC.srcEq]
    exact List.mem_flatMap.mpr ⟨p, hmem hz, hl⟩
  -- A borrow the callee may hand out is one the caller expects.
  have hback : ∀ {L : List Loan} {b : Nat} (r : Bool), Good Y st1 L b →
      (∀ l ∈ L, exitOK Rules.full F r l = true) → Good X st2 src b := by
    rintro L b r ⟨ha, hkb, l, hl, hcov⟩ hx
    have hxl := hx l hl
    cases l with
    | addr v => simp [exitOK, Rules.full] at hxl
    | glob => cases r <;> simp [exitOK, Rules.full] at hxl
    | par j =>
      obtain ⟨ne, v, t, L, hz, hne, ht, hte, hL, hG⟩ := hQ j b hcov.1
      refine (good_eff hI hM hW hE (hnk hL).1 hG).mono fun l hl => hsrc hz l ?_
      simp only [exitOK, Rules.full, Bool.not_true, Bool.false_or, Bool.or_eq_true, beq_iff_eq,
        Bool.not_eq_true'] at hxl
      rcases hte with rfl | rfl
      · rw [ht, hne] at hxl
        simp at hxl
        simp [argSrc, hxl, hL, hl]
      · simp [argSrc, hL, hl]
    | own j =>
      obtain ⟨c, ho, hc⟩ := hcov
      have hko := hI1.wf.own c b (hI1.ocell ho).1 (hI1.ocell ho).2 hc
      have ha2 : st2.alive b = true := by rw [← hst2]; exact leave_heap hI1 ha (.inl hko.2)
      simp only [exitOK, Rules.full, Bool.not_true, Bool.false_or] at hxl
      split at hxl
      · rename_i hty
        have : ∃ t, F.tys[j]? = some t ∧ localTy t = false ∧ (t = .sh ∨ t = .ex) := by
          rcases hty with hi | hi <;> exact ⟨_, hi, rfl, by simp⟩
        obtain ⟨t, hj, hlo, hte⟩ := this
        obtain ⟨ne, v, hz, hne⟩ := zip_of hC.len hj (hparam hj hlo)
        obtain ⟨c', hv, ho', _⟩ := hobj hz hte
        have hcc : c = c' := by
          rcases ho with ⟨h, _⟩ | ⟨_, h⟩
          · have h : F.tys[j]? = some .own := h
            rw [h] at hty; simp at hty
          · exact h.trans (ha' hv)
        subst hcc
        refine ⟨ha2, by rw [hk2]; exact hkb, .own v, hsrc hz _ ?_, c, ho', by rw [hc2]; exact hc⟩
        rw [hne] at hxl
        rcases hte with rfl | rfl <;> simp_all [argSrc, Rules.full]
      · cases hxl
  have hccs : ∀ {v c : Nat}, CCell X st2 v c → CCell X st v c := by
    intro v c hcc
    rcases hcc with h | ⟨h, h'⟩ | h
    · exact .inl h
    · refine .inr (.inl ⟨h, ?_⟩)
      rw [hE.cont _ (hI.wf.lt _ (hI.fr v _ h).1) (fun hm => ?_) (hI.not_w hW h (by simp) (by simp))]
        at h'
      · exact h'
      · rcases hm with h0 | ⟨u, _, ho⟩
        · have := hI.fp2; omega
        · have := (hI.ocell ho).1; rw [(hI.fr v _ h).2] at this; cases this
    · exact .inr (.inr h)
  have hfin := inv_eff (A' := A2) hI hM hW hwf2 hE (fun v c hcc => ?_) (fun q t L h1 h2 h3 => ?_)
  · -- The returned value.
    cases ret with
    | none =>
      simp only at hret
      subst hret
      simpa [Post] using hfin
    | some y =>
      simp only at hret
      obtain ⟨hy, hrs, rfl⟩ := hret
      obtain ⟨r, hr⟩ := Option.isSome_iff_exists.mp hrs
      simp only [hr] at hexitR ⊢
      simp only [Bool.and_eq_true, beq_iff_eq] at hexitR
      obtain ⟨hrt, hexitR⟩ := hexitR
      split at hexitR
      · rename_i L hL
        have hv : GoodV X st2 (some src) (st1.cont (st.next + r)) := by
          intro L' b hL' hb
          cases hL'
          exact hback true (hI1.val r _ (.inl ⟨hrt, rfl⟩) L b hL hb) (List.all_eq_true.mp hexitR)
        have := inv_setBor hfin.1 hy hv
        exact ⟨this.1, hfin.2.trans this.2⟩
      · cases hexitR
  · -- What the caller's cells hold.
    have hcc := hccs hcc
    intro L b hL hb
    obtain ⟨M, hB, hsub, hfl⟩ := hC.flow v L hL
    by_cases hw : WC Y c
    · obtain ⟨i, hi, rfl⟩ := hw
      obtain ⟨ne, w, hz, _⟩ := zip_of hC.len hi (hparam hi rfl)
      obtain ⟨c, hvi, hccw⟩ := hout hz
      change st2.cont (a' i) = _ at hb
      change CCell X st v (a' i) at hcc
      rw [ha' hvi] at hb hcc
      have hwo : w ∈ outArgs as := mem_outArgs.mpr ⟨i, ne, hz⟩
      have hsl : ∀ l ∈ src, l ∈ L := by
        apply hfl
        rcases hI.ccell_inj hcc hccw with rfl | ⟨h1, h2⟩
        · exact .inl hwo
        · exact .inr ⟨⟨w, hwo, h2⟩, h1⟩
      have hex := hexit i (List.mem_range.mpr (List.getElem?_eq_some_iff.mp hi).1)
      rw [hi, Bool.or_eq_true] at hex
      replace hex := hex.resolve_left (by decide)
      split at hex
      · rename_i L1 hL1
        refine (hback false (hI1.val i _ (.inr (.inr ⟨hi, rfl⟩)) L1 b hL1 ?_)
          (List.all_eq_true.mp hex)).mono hsl
        rw [hc2] at hb
        show st1.cont (a' i) = _
        rw [ha' hvi]; exact hb
      · simp [Rules.full] at hex
    · have hcl := hI.wf.lt c (hck hcc).2.1
      have hnm : ¬ MC Y c := fun h => by
        have h1 := (hI.mok hM (hMC c h)).1
        have h2 := (hck hcc).1
        rcases h2 with h2 | h2 <;> rw [h2] at h1 <;> simp at h1
      rw [hc2, hE1.cont c hcl hnm hw, (enter_old hcl).2.1] at hb
      exact (good_eff hI hM hW hE (hnk hB).1 (hI.val v c hcc M b (hnk hB).2 hb)).mono hsub
  · have hq := hI.fr q _ h1
    have ht := hI.fr t _ h2
    obtain ⟨M, hB, hsub, _⟩ := hC.flow q L h3
    have hnotM : ∀ {w : Nat} {s : Ty}, X.D[w]? = some s → s ≠ .own → ¬ M0 (X.fp + w) := by
      rintro w s hs hne (h0 | ⟨u, _, ho⟩)
      · have := hI.fp2; omega
      · have := (hI.ocell ho).1
        rw [(hI.fr w _ hs).2] at this; cases s <;> simp_all [Ty.kind]
    have hnotW : ¬ W0 (X.fp + q) := by
      rintro ⟨u, _, hcc⟩
      have := (hck hcc).1
      rw [hq.2] at this; simp [Ty.kind] at this
    rw [hE.cont _ (hI.wf.lt _ hq.1) (hnotM h1 (by simp)) hnotW,
      hE.cont _ (hI.wf.lt _ ht.1) (hnotM h2 (by simp)) (hI.not_w hW h2 (by simp) (by simp))]
    have := hI.ali q t M h1 h2 (hnk hB).2
    exact ⟨hsub _ this.1, this.2⟩

/-! ## Statements -/

theorem inv_weaken {X : Ctx} {A A' : Abs} {st : St} (hI : Inv X A st)
    (h : ∀ v L, A' v = some L → ∃ M, A v = some M ∧ ∀ l ∈ M, l ∈ L) : Inv X A' st := by
  refine ⟨hI.wf, hI.fp2, hI.le, hI.fr, hI.tgt, hI.obj, hI.outk, hI.apart, fun v c hc L b hL hb => ?_,
    fun q t L h1 h2 h3 => ?_, hI.fresh⟩
  · obtain ⟨M, hM, hs⟩ := h v L hL
    exact (hI.val v c hc M b hM hb).mono hs
  · obtain ⟨M, hM, hs⟩ := h q L h3
    have := hI.ali q t M h1 h2 hM
    exact ⟨hs _ this.1, this.2⟩

theorem join_some {A B : Abs} {v : Nat} {L : List Loan} (h : join A B v = some L) :
    (∃ M, A v = some M ∧ ∀ l ∈ M, l ∈ L) ∧ (∃ M, B v = some M ∧ ∀ l ∈ M, l ∈ L) := by
  unfold join at h
  split at h
  · rename_i L1 L2 h1 h2
    cases h
    exact ⟨⟨L1, h1, fun l h => List.mem_append_left _ h⟩,
      ⟨L2, h2, fun l h => List.mem_append_right _ h⟩⟩
  · cases h

theorem Post.seq {X : Ctx} {A1 A2 : Abs} {st : St} {o : Out} {g : St → Out}
    (h : Post X A1 st o) (hg : ∀ st', Inv X A1 st' → Post X A2 st' (g st')) :
    Post X A2 st (o.then g) := by
  cases o with
  | ok st' =>
    have h2 := hg st' h.1
    simp only [Out.then]
    revert h2
    cases g st' with
    | ok st2 => exact fun h2 => ⟨h2.1, h.2.trans h2.2⟩
    | ub k => exact fun h2 => h2
    | trap => exact fun _ => trivial
    | outOfFuel => exact fun _ => trivial
  | ub k => exact h
  | trap => trivial
  | outOfFuel => trivial

theorem reach_post {X : Ctx} {A : Abs} {st : St} {v : Val} {g : Nat → Out}
    (ha : ∀ b, v = .ptr b → st.alive b = true) (hg : ∀ b, v = .ptr b → Post X A st (g b)) :
    Post X A st (st.reach v g) := by
  cases v with
  | null => trivial
  | ptr b => simp only [St.reach, ha b rfl, ite_true]; exact hg b rfl

theorem ownCell_post {X : Ctx} {A A' : Abs} {st : St} (hI : Inv X A st) {v : Nat} {g : Nat → Out}
    (hd : X.D[v]? = some .own ∨ X.D[v]? = some .sh ∨ X.D[v]? = some .ex)
    (hg : ∀ c, OCell X v c → Post X A' st (g c)) : Post X A' st (ownCell X.D X.fp st v g) := by
  rcases hd with hd | hd
  · simp only [ownCell, hd]; exact hg _ (.inl ⟨hd, rfl⟩)
  · have ht := hI.tgt v (.inr hd)
    have : ownCell X.D X.fp st v g = st.reach (st.cont (X.fp + v)) g := by
      rcases hd with hd | hd <;> simp only [ownCell, hd]
    rw [this]
    refine reach_post (fun b hb => ?_) (fun b hb => ?_) <;> rw [ht.1] at hb <;> cases hb
    · exact ht.2.1
    · exact hg _ (.inr ⟨hd, rfl⟩)

theorem free_post {X : Ctx} {A : Abs} {st : St} (hI : Inv X A st) {l : Loan} {c : Nat}
    (hl : Names X l c) (hk : st.kind c = .ocell) (ha : st.alive c = true) :
    Post X (kill l A) st (freeCell st c) := by
  unfold freeCell
  split
  · exact ⟨inv_weaken hI fun v L h => ⟨L, (kill_some h).1, fun _ h => h⟩, Eff.refl _ _ _ _⟩
  · rename_i b hb
    rw [(hI.wf.own c b hk ha hb).1]
    exact inv_owner hI hl (wf_free hI.wf ha (.inl hk) hb) (eff_free hb)

/-- The main induction: an accepted statement keeps the invariant, touches only
what its context allows, and is never UB. -/
theorem exec_ok (P : List Fn) (hP : ∀ F ∈ P, chkFn Rules.full P F = true) :
    ∀ (n : Nat) (X : Ctx) (A A' : Abs) (st : St) (s : Stmt), Inv X A st →
      chk Rules.full P X.D A s = some A' → Post X A' st (exec P n X.D X.fp st s) := by
  intro n
  induction n with
  | zero => intros; simp [exec, Post]
  | succ n ih =>
  intro X A A' st s hI hc
  cases s with
  | skip =>
    simp only [chk, Option.some.injEq] at hc; subst hc
    exact ⟨hI, Eff.refl _ _ _ _⟩
  | seq a b =>
    simp only [chk, Option.bind_eq_some_iff] at hc
    obtain ⟨A1, h1, h2⟩ := hc
    simp only [exec]
    exact (ih X A A1 st a hI h1).seq fun st' hI' => ih X A1 A' st' b hI' h2
  | branch c a b =>
    simp only [chk, Option.bind_eq_some_iff, Option.some.injEq] at hc
    obtain ⟨A1, h1, A2, h2, rfl⟩ := hc
    simp only [exec]
    split
    · have := ih X A A1 st a hI h1
      revert this
      cases exec P n X.D X.fp st a with
      | ok st' => exact fun h => ⟨inv_weaken h.1 fun v L hL => (join_some hL).1, h.2⟩
      | _ => exact fun h => h
    · have := ih X A A2 st b hI h2
      revert this
      cases exec P n X.D X.fp st b with
      | ok st' => exact fun h => ⟨inv_weaken h.1 fun v L hL => (join_some hL).2, h.2⟩
      | _ => exact fun h => h
  | null x =>
    simp only [chk] at hc
    split at hc <;> cases hc
    exact inv_setBor hI ‹_› (fun L b _ h => by cases h)
  | addr x y =>
    simp only [chk] at hc
    split at hc <;> cases hc
    rename_i h
    refine inv_setBor hI h.1 (fun L b hL hb => ?_)
    cases hL; cases hb
    have := hI.fr y _ h.2
    exact ⟨this.1, .inr this.2, _, List.mem_singleton.mpr rfl, h.2, rfl⟩
  | copy x y =>
    simp only [chk] at hc
    split at hc <;> cases hc
    rename_i h
    exact inv_setBor hI h.1 (hI.val y _ (.inl ⟨h.2.1, rfl⟩))
  | deref x =>
    simp only [chk] at hc
    split at hc <;> cases hc
    rename_i h
    obtain ⟨L, hL⟩ := Option.isSome_iff_exists.mp h.2
    simp only [exec]
    exact reach_post (fun b hb => (hI.val x _ (.inl ⟨h.1, rfl⟩) L b hL hb).1)
      (fun _ _ => ⟨hI, Eff.refl _ _ _ _⟩)
  | new v =>
    simp only [chk] at hc
    split at hc <;> cases hc
    rename_i h
    simp only [exec]
    refine ownCell_post hI (h.imp id .inr) fun c ho => ?_
    have hoc := hI.ocell ho
    refine inv_owner hI (l := .own v) ⟨ho, h⟩ (wf_alloc hI.wf hoc.2 ?_ (.inl ⟨hoc.1, rfl⟩)) (eff_alloc)
    rintro rfl; rw [hI.wf.g1.1] at hoc; cases hoc.1
  | free v =>
    simp only [chk] at hc
    split at hc <;> cases hc
    rename_i h
    simp only [exec]
    rcases h with h | h | h
    · refine ownCell_post hI (.inl h) fun c ho => ?_
      exact free_post hI (l := .own v) ⟨ho, .inl h⟩ (hI.ocell ho).1 (hI.ocell ho).2
    · refine ownCell_post hI (.inr (.inr h)) fun c ho => ?_
      exact free_post hI (l := .own v) ⟨ho, .inr h⟩ (hI.ocell ho).1 (hI.ocell ho).2
    · simp only [ownCell, h]
      have hf := hI.fr v _ h
      unfold freeCell
      split
      · exact ⟨inv_weaken hI fun v L h => ⟨L, (kill_some h).1, fun _ h => h⟩, Eff.refl _ _ _ _⟩
      · rename_i b hb
        rw [(hI.wf.view _ b hf.2 hf.1 hb).1]
        refine inv_vowner hI h (wf_free hI.wf hf.1 (.inr hf.2) hb) (eff_free hb)
          (fun w L _ hL => (kill_some hL).1) (.inl (by simp [St.set, upd])) (fun q L hq hs => (kill_some hs).2)
  | get x v =>
    simp only [chk] at hc
    split at hc <;> cases hc
    rename_i h
    simp only [exec]
    refine ownCell_post hI h.2 fun c ho => inv_setBor hI h.1 (fun L b hL hb => ?_)
    cases hL
    have hoc := hI.ocell ho
    have := hI.wf.own c b hoc.1 hoc.2 hb
    exact ⟨this.1, .inl this.2, _, List.mem_singleton.mpr rfl, c, ho, hb⟩
  | newG =>
    simp only [chk, Option.some.injEq] at hc; subst hc
    exact inv_owner hI (l := .glob) rfl
      (wf_alloc hI.wf hI.wf.g0.1 (by decide) (.inl ⟨hI.wf.g0.2, rfl⟩)) eff_alloc
  | freeG =>
    simp only [chk, Option.some.injEq] at hc; subst hc
    exact free_post hI (l := .glob) rfl hI.wf.g0.2 hI.wf.g0.1
  | getG x =>
    simp only [chk] at hc
    split at hc <;> cases hc
    rename_i h
    refine inv_setBor hI h (fun L b hL hb => ?_)
    cases hL
    have := hI.wf.own 0 b hI.wf.g0.2 hI.wf.g0.1 hb
    exact ⟨this.1, .inl this.2, _, List.mem_singleton.mpr rfl, hb⟩
  | newv t =>
    simp only [chk] at hc
    split at hc <;> cases hc
    rename_i h
    have hf := hI.fr t _ h
    have hne : st.next ≠ X.fp + t := by have := hI.wf.lt _ hf.1; omega
    refine inv_vowner hI h (wf_alloc hI.wf hf.1 ?_ (.inr ⟨hf.2, rfl⟩)) eff_alloc
      (fun w L hw hL => ?_) (.inr ⟨by simp [St.set, upd], by simp [St.set, St.grow, upd, hne]⟩)
      (fun q L hq hs => ?_)
    · have := hI.fp2; omega
    · simp only [upd, ite_eq_right hw] at hL; exact (kill_some hL).1
    · have hqt : q ≠ t := by rintro rfl; rw [h] at hq; cases hq
      simp only [upd, ite_eq_right hqt] at hs; exact (kill_some hs).2
  | mkalias q =>
    simp only [chk] at hc
    split at hc
    · rename_i t hq
      split at hc
      · rename_i ht
        simp only [Option.bind_eq_some_iff, Option.some.injEq] at hc
        obtain ⟨L, hL, rfl⟩ := hc
        simp only [exec, hq]
        have hf := hI.fr q _ hq
        have hW : WOK X st (· = X.fp + q) := by
          rintro d rfl
          exact ⟨.inr ⟨q, t, hq, rfl⟩, .inr (Nat.le_add_right _ _), hf.1⟩
        have hcne : ∀ {v c : Nat}, CCell X st v c → c ≠ X.fp + q := by
          rintro v c hcc rfl
          have hk := hf.2
          rcases hcc with ⟨h, h'⟩ | ⟨h, h'⟩ | ⟨h, h'⟩
          · rw [h', (hI.fr v _ h).2] at hk; cases hk
          · rw [(hI.wf.view _ _ (hI.fr v _ h).2 (hI.fr v _ h).1 h').2] at hk; cases hk
          · rw [h'] at hk; rcases hI.outk v h with h | h <;> rw [h] at hk <;> cases hk
        refine inv_eff hI mok_false hW (wf_set hI.wf (by rw [hf.2]; simp [Ty.kind]) _)
          (eff_set _ _ _ _) (fun v d hd => ?_) (fun r u M h1 h2 h3 => ?_)
        · replace hd := ccell_set (fun h => frNe h hq (by simp)) hd
          have hne := hcne hd
          have hvq : v ≠ q := by
            rintro rfl
            rcases hd with ⟨h, _⟩ | ⟨h, _⟩ | ⟨h, _⟩ <;> rw [hq] at h <;> cases h
          simp only [St.set, upd, ite_eq_right hne, ite_eq_right hvq]
          exact goodV_set hI hW _ (hI.val v d hd)
        · have hun := frNe h2 hq (by simp)
          by_cases hr : r = q
          · subst hr
            rw [hq] at h1; cases h1
            simp only [upd, ite_true, Option.some.injEq] at h3
            subst h3
            exact ⟨List.mem_cons_self .., .inr (by simp [St.set, upd])⟩
          · have hrq : X.fp + r ≠ X.fp + q := fun h => hr (Nat.add_left_cancel h)
            simp only [upd, ite_eq_right hr] at h3
            have := hI.ali r u M h1 h2 h3
            simpa only [St.set, upd, ite_eq_right hrq, ite_eq_right hun] using this
      · cases hc
    · cases hc
  | load x q =>
    simp only [chk] at hc
    split at hc
    case isFalse => cases hc
    rename_i hx
    simp only [exec]
    have fin : ∀ {w c : Nat} {o : Option (List Loan)}, CCell X st w c → A w = o →
        Post X (upd A x o) st (.ok (st.set (X.fp + x) (st.cont c))) := by
      rintro w c o hcc rfl
      exact inv_setBor hI hx (hI.val w c hcc)
    have viewP : ∀ {t : Nat} {v : Val} {o : Option (List Loan)}, X.D[t]? = some .vown →
        (v = .null ∨ v = st.cont (X.fp + t)) → A t = o →
        Post X (upd A x o) st (st.reach v fun c => .ok (st.set (X.fp + x) (st.cont c))) := by
      rintro t v o ht (rfl | rfl) ho
      · trivial
      · have hf := hI.fr t _ ht
        exact reach_post (fun b hb => (hI.wf.view _ b hf.2 hf.1 hb).1)
          (fun b hb => fin (.inr (.inl ⟨ht, hb⟩)) ho)
    split at hc
    · rename_i hq
      simp only [Option.bind_eq_some_iff, Option.some.injEq] at hc
      obtain ⟨L, hL, rfl⟩ := hc
      have ht := hI.tgt q (.inl hq)
      refine reach_post (fun b hb => ?_) (fun b hb => ?_) <;> rw [ht.1] at hb <;> cases hb
      · exact ht.2.1
      · exact fin (.inr (.inr ⟨hq, rfl⟩)) hL
    · rename_i hq
      simp only [Option.bind_eq_some_iff, Option.some.injEq] at hc
      obtain ⟨L, hL, rfl⟩ := hc
      exact viewP hq (.inr rfl) hL
    · rename_i t hq
      split at hc
      case isFalse => cases hc
      rename_i ht
      simp only [Option.bind_eq_some_iff, Rules.full, ite_true, Option.some.injEq] at hc
      obtain ⟨L, hL, M, hM, rfl⟩ := hc
      exact viewP ht (hI.ali q t L hq ht hL).2 hM
    · cases hc
  | store q x =>
    simp only [chk] at hc
    split at hc
    case isFalse => cases hc
    rename_i hx
    simp only [Option.bind_eq_some_iff] at hc
    obtain ⟨L, hL, hc⟩ := hc
    have hxv : GoodV X st (some L) (st.cont (X.fp + x)) := by
      rw [← hL]; exact hI.val x _ (.inl ⟨hx, rfl⟩)
    simp only [exec]
    split at hc
    · rename_i hq
      simp only [Rules.full, ite_true, Option.some.injEq] at hc
      subst hc
      have hf := hI.fr q _ hq
      refine reach_post (fun b hb => (hI.wf.view _ b hf.2 hf.1 hb).1) (fun c hb => ?_)
      have hcc : CCell X st q c := .inr (.inl ⟨hq, hb⟩)
      refine inv_store hI hcc (hI.wf.view _ c hf.2 hf.1 hb).1 (fun v L' hL' => ?_) (fun r t hr => ?_)
      · by_cases hv : v = q
        · subst hv
          simp only [upd, ite_true, Option.map_eq_some_iff] at hL'
          obtain ⟨M, _, rfl⟩ := hL'
          exact ⟨L, fun l h => List.mem_append_left _ h, .inl ⟨hcc, hxv⟩⟩
        · simp only [upd, ite_eq_right hv] at hL'
          refine ⟨L', fun _ h => h, .inr ⟨fun h => ?_, hL'⟩⟩
          rcases hI.ccell_inj h hcc with h | ⟨_, h⟩
          · exact hv h
          · rw [hq] at h; cases h
      · have : r ≠ q := by rintro rfl; rw [hq] at hr; cases hr
        simp only [upd, ite_eq_right this]
    · rename_i hq
      simp only [Rules.full, true_and, Option.some.injEq] at hc
      subst hc
      have ht := hI.tgt q (.inl hq)
      have hcc : CCell X st q (X.a q) := .inr (.inr ⟨hq, rfl⟩)
      refine reach_post (fun b hb => ?_) (fun b hb => ?_) <;> rw [ht.1] at hb <;> cases hb
      · exact ht.2.1
      refine inv_store hI hcc ht.2.1 (fun v L' hL' => ?_) (fun r t hr => ?_)
      · by_cases hv : v = q
        · subst hv
          simp only [ite_true, Option.some.injEq] at hL'
          subst hL'
          exact ⟨L, fun l h => h, .inl ⟨hcc, hxv⟩⟩
        · simp only [ite_eq_right hv] at hL'
          split at hL'
          · rename_i hvo
            simp only [Option.map_eq_some_iff] at hL'
            obtain ⟨M, hM, rfl⟩ := hL'
            by_cases hcv : CCell X st v (X.a q)
            · exact ⟨L, fun l h => List.mem_append_left _ h, .inl ⟨hcv, hxv⟩⟩
            · exact ⟨M, fun l h => List.mem_append_right _ h, .inr ⟨hcv, hM⟩⟩
          · rename_i hvo
            refine ⟨L', fun _ h => h, .inr ⟨fun h => ?_, hL'⟩⟩
            rcases hI.ccell_inj h hcc with h | ⟨h, _⟩
            · exact hv h
            · exact hvo h
      · have h1 : r ≠ q := by rintro rfl; rw [hq] at hr; cases hr
        have h2 : ¬ X.D[r]? = some .out := by rw [hr]; simp
        simp only [ite_eq_right h1, ite_eq_right h2]
    · simp [Rules.full] at hc
    · cases hc
  | gput t => simp [chk, Rules.full] at hc
  | gget t => simp [chk, Rules.full] at hc
  | call f args ret =>
    simp only [chk, Option.bind_eq_some_iff] at hc
    obtain ⟨F, hF, hc⟩ := hc
    exact call_ok hI hF (hP F (List.mem_of_getElem? hF))
      (fun X A A' st _ hI h => ih X A A' st F.body hI h) hc

/-! ## The theorem -/

theorem wf_init : WF St.init := by
  refine ⟨fun b h => by simpa [St.init] using h, by decide, by simp [St.init], by simp [St.init],
    fun c b _ _ h => ?_, fun c b _ _ h => ?_, fun c c' b _ _ _ _ h => ?_⟩ <;> cases h

/-- If every body passes the checker under the full rules, running the program
never reaches UB, for any fuel. -/
theorem out_sound (P : List Fn) (h : check Rules.full P = true) (fuel : Nat) :
    (run P fuel).isUB = false := by
  simp only [check, Bool.and_eq_true] at h
  obtain ⟨hP, hm⟩ := h
  rw [List.all_eq_true] at hP
  split at hm
  case h_2 => cases hm
  rename_i F hF
  have hI : Inv ⟨[], 2, fun _ => 0, fun _ _ => False⟩ (fun _ => none) St.init := by
    refine ⟨wf_init, Nat.le_refl _, Nat.le_refl _, ?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩ <;>
      simp [CCell]
  have hc : (chk Rules.full P [] (fun _ => none) (.call 0 [] none)).isSome = true := by
    simp [chk, hF, chkCall, List.isEmpty_iff.mp hm, exArgs, Rules.full]
  obtain ⟨A', hA'⟩ := Option.isSome_iff_exists.mp hc
  have := exec_ok P hP fuel _ _ A' _ _ hI hA'
  unfold run
  revert this
  show Post _ _ _ (exec P fuel [] 2 St.init (.call 0 [] none)) → _
  cases exec P fuel [] 2 St.init (.call 0 [] none) with
  | ub k => exact fun h => h.elim
  | _ => exact fun _ => rfl

#print axioms out_sound

end SafeC.Out
