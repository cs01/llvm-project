-- Soundness: under `Rules.Sound`, a well-typed program with no `__unsafe`
-- block never reaches UB, for any amount of fuel.
import SafeC.Defs

namespace SafeC

/-- The rule changes the proof needs (see README, findings 1-4). -/
def Rules.Sound (R : Rules) : Prop :=
  (R.nullTrap = true ∨ (R.zeroNonnull = false ∧ R.callKeepsNarrowing = false)) ∧
  R.castAny = false ∧ R.ntBounded = true

/-! ## The invariant -/

/-- A block of element type `a` may be viewed through a `t *`. -/
def compat (a t : Ty) : Prop := t = .void ∨ a = t ∨ (a.ptrFree = true ∧ t.ptrFree = true)

def kindOK (len : Nat) (off lo hi : Int) : Kind → Prop
  | .single => 0 ≤ off ∧ off < len
  | .counted n => 0 ≤ off ∧ off + n ≤ len
  | .indexable => 0 ≤ lo ∧ hi ≤ len
  | .nullterm => 0 ≤ lo ∧ hi ≤ len

/-- Value `v` is a valid `τ` in memory `μ`. `st` (strict) is true when null
accesses are UB; only then must "nonnull" be true of the value. -/
def conf (st : Bool) (μ : Mem) : Val → Ty → Prop
  | .int _, .int => True
  | .int _, .char => True
  | .null, .ptr q _ _ => q = .nullable ∨ st = false
  | .ptr b off lo hi, .ptr _ k t =>
    ∃ blk, μ[b]? = some blk ∧ compat blk.ty t ∧ kindOK blk.cells.length off lo hi k
  | _, _ => False

abbrev WFMem (st : Bool) (μ : Mem) : Prop :=
  ∀ (b : Nat) (blk : Block), μ[b]? = some blk → ∀ v ∈ blk.cells, conf st μ v blk.ty

/-- Memory only grows; blocks keep their type and length. -/
abbrev Ext (μ μ' : Mem) : Prop :=
  ∀ (b : Nat) (blk : Block), μ[b]? = some blk → ∃ blk' : Block, μ'[b]? = some blk' ∧ blk'.ty = blk.ty ∧ blk'.cells.length = blk.cells.length

abbrev InvE (st : Bool) (D : Decls) (F : Facts) (ρ : Env) (μ : Mem) : Prop :=
  ∀ x τ, D.lookup x = some τ → x ∈ F.init → conf st μ (ρ x) τ ∧ (st = true → x ∈ F.nn → ρ x ≠ .null)

def Safe {α : Type} (P : α → Prop) : Res α → Prop
  | .ok a => P a
  | .trap => True
  | .ub => False

def SafeO (P : Env → Mem → Prop) : Out → Prop
  | .ok ρ μ => P ρ μ
  | .ub => False
  | _ => True

/-! ## Plumbing -/

theorem Safe.bind {α β : Type} {P : α → Prop} {Q : β → Prop} {m : Res α} {f : α → Res β}
    (h : Safe P m) (hf : ∀ a, P a → Safe Q (f a)) : Safe Q (m.bind f) := by
  cases m with
  | ok a => exact hf a h
  | trap => exact True.intro
  | ub => exact False.elim h

theorem Safe.mono {α : Type} {P Q : α → Prop} {m : Res α} (h : Safe P m) (hf : ∀ a, P a → Q a) :
    Safe Q m := by
  cases m with
  | ok a => exact hf a h
  | trap => exact True.intro
  | ub => exact False.elim h

theorem SafeO_rthen {α : Type} {P : α → Prop} {Q : Env → Mem → Prop} {r : Res α} {f : α → Out}
    (h : Safe P r) (hf : ∀ a, P a → SafeO Q (f a)) : SafeO Q (r.then f) := by
  cases r with
  | ok a => exact hf a h
  | trap => exact True.intro
  | ub => exact False.elim h

theorem SafeO_othen {P Q : Env → Mem → Prop} {o : Out} {f : Env → Mem → Out}
    (h : SafeO P o) (hf : ∀ ρ μ, P ρ μ → SafeO Q (f ρ μ)) : SafeO Q (o.then f) := by
  cases o with
  | ok ρ μ => exact hf ρ μ h
  | trap => exact True.intro
  | ub => exact False.elim h
  | outOfFuel => exact True.intro

theorem SafeO_mono {P Q : Env → Mem → Prop} {o : Out} (h : SafeO P o) (hf : ∀ ρ μ, P ρ μ → Q ρ μ) :
    SafeO Q o := by
  cases o with
  | ok ρ μ => exact hf ρ μ h
  | trap => exact True.intro
  | ub => exact False.elim h
  | outOfFuel => exact True.intro

theorem conf_mono {st : Bool} {μ μ' : Mem} {v : Val} {t : Ty} (h : Ext μ μ') (hc : conf st μ v t) :
    conf st μ' v t := by
  cases v <;> cases t <;> first
    | exact hc
    | (obtain ⟨blk, hb, hcomp, hk⟩ := hc
       obtain ⟨blk', hb', hty, hlen⟩ := h _ _ hb
       exact ⟨blk', hb', by rw [hty]; exact hcomp, by rw [hlen]; exact hk⟩)

theorem InvE_mono {st D F ρ} {μ μ' : Mem} (h : Ext μ μ') (hI : InvE st D F ρ μ) : InvE st D F ρ μ' :=
  fun x τ hx hi => ⟨conf_mono h (hI x τ hx hi).1, (hI x τ hx hi).2⟩

theorem InvE_weaken {st D ρ μ} {F G : Facts} (hI : InvE st D F ρ μ) (h1 : G.init ⊆ F.init)
    (h2 : G.nn ⊆ F.nn) : InvE st D G ρ μ :=
  fun x τ hx hi => ⟨(hI x τ hx (h1 hi)).1, fun hs hn => (hI x τ hx (h1 hi)).2 hs (h2 hn)⟩

/-- When null accesses trap, narrowing facts carry no obligation. -/
theorem InvE_init_only {st D ρ μ} {F G : Facts} (hst : st = false) (hI : InvE st D G ρ μ)
    (h : F.init ⊆ G.init) : InvE st D F ρ μ :=
  fun x τ hx hi => ⟨(hI x τ hx (h hi)).1, fun hs => by rw [hst] at hs; cases hs⟩

theorem InvE_narrow {st D ρ μ} {F : Facts} {x : Var} (hI : InvE st D F ρ μ) (hx : ρ x ≠ .null) :
    InvE st D ⟨F.init, x :: F.nn⟩ ρ μ :=
  fun y τ hy hi => ⟨(hI y τ hy hi).1, fun hs hn => by
    rcases List.mem_cons.1 hn with rfl | hn
    · exact hx
    · exact (hI y τ hy hi).2 hs hn⟩

theorem InvE_assign {st D ρ μ} {F : Facts} {x : Var} {σ : Ty} {v : Val} (hI : InvE st D F ρ μ)
    (hx : D.lookup x = some σ) (hv : conf st μ v σ) : InvE st D (F.assign x) (ρ.set x v) μ := by
  intro y τ hy hi
  simp only [Facts.assign, List.mem_cons] at hi
  by_cases hyx : y = x
  · subst hyx
    rw [hx] at hy; cases hy
    refine ⟨by simp only [Env.set]; exact hv, fun _ hn => ?_⟩
    simp [Facts.assign, List.mem_filter] at hn
  · rcases hi with h | h
    · exact absurd h hyx
    · refine ⟨by simp only [Env.set, ite_eq_right hyx]; exact (hI y τ hy h).1, fun hs hn => ?_⟩
      simp only [Env.set, ite_eq_right hyx]
      exact (hI y τ hy h).2 hs (List.mem_filter.1 hn).1

theorem meet_sub {F G : Facts} :
    (F.meet G).init ⊆ F.init ∧ (F.meet G).nn ⊆ F.nn ∧ (F.meet G).init ⊆ G.init ∧ (F.meet G).nn ⊆ G.nn := by
  refine ⟨fun x hx => ?_, fun x hx => ?_, fun x hx => ?_, fun x hx => ?_⟩ <;>
    simp only [Facts.meet, List.mem_filter, decide_eq_true_eq] at hx <;> simp [hx]

theorem InvE_empty {st D ρ μ} : InvE st D Facts.empty ρ μ := fun _ _ _ h => by cases h

theorem WFMem_nil {st} : WFMem st [] := fun _ _ h => by simp at h

/-! ## Values -/

theorem conf_pf {st μ v} {a t : Ty} (ha : a.ptrFree = true) (ht : t.ptrFree = true) (hc : conf st μ v a) :
    conf st μ v t := by
  cases a <;> simp [Ty.ptrFree] at ha <;> cases t <;> simp [Ty.ptrFree] at ht <;>
    cases v <;> first | exact True.intro | exact hc

theorem int_of_pf {st μ v} {τ : Ty} (h : τ.ptrFree = true) (hc : conf st μ v τ) : ∃ n, v = .int n := by
  cases τ <;> simp [Ty.ptrFree] at h <;> cases v <;> first | exact ⟨_, rfl⟩ | exact False.elim hc

theorem conf_of_compat {st μ v} {a t : Ty} (hc : compat a t) (ht : t ≠ .void) (hv : conf st μ v a) :
    conf st μ v t := by
  rcases hc with h | rfl | ⟨h1, h2⟩
  · exact absurd h ht
  · exact hv
  · exact conf_pf h1 h2 hv

theorem conf_to_compat {st μ v} {a t : Ty} (hc : compat a t) (ht : t ≠ .void) (hv : conf st μ v t) :
    conf st μ v a := by
  rcases hc with h | rfl | ⟨h1, h2⟩
  · exact absurd h ht
  · exact hv
  · exact conf_pf h2 h1 hv

theorem conf_sub {st μ v} {τ σ : Ty} (hs : Sub τ σ) (hc : conf st μ v τ) : conf st μ v σ := by
  rcases hs with rfl | ⟨h1, h2⟩ | ⟨k, t, rfl, rfl⟩
  · exact hc
  · exact conf_pf h1 h2 hc
  · cases v <;> first | exact hc | exact Or.inl rfl

theorem compat_cast {R : Rules} {a t t' : Ty} (hR : R.castAny = false) (hc : compat a t)
    (h : castOK R false t t' = true) : compat a t' := by
  simp [castOK, hR] at h
  unfold compat at hc ⊢
  rcases h with h | ⟨hnv, h⟩
  · exact Or.inl h
  · rcases h with rfl | ⟨h1, h2⟩
    · exact hc
    · rcases hc with hc | rfl | ⟨hc1, _⟩
      · exact absurd hc hnv
      · exact Or.inr (Or.inr ⟨h1, h2⟩)
      · exact Or.inr (Or.inr ⟨hc1, h2⟩)

theorem conf_cast {R : Rules} {st μ v q k} {t t' : Ty} (hR : R.castAny = false)
    (h : castOK R false t t' = true) (hc : conf st μ v (.ptr q k t)) : conf st μ v (.ptr q k t') := by
  cases v with
  | ptr b off lo hi =>
    obtain ⟨blk, hb, hcomp, hk⟩ := hc
    exact ⟨blk, hb, compat_cast hR hcomp h, hk⟩
  | null => exact hc
  | int _ => exact False.elim hc
  | uninit => exact False.elim hc

theorem conf_narrow {st μ v} {σ : Ty} {F : Facts} {x : Var} (hc : conf st μ v σ)
    (hn : st = true → x ∈ F.nn → v ≠ .null) : conf st μ v (narrow F x σ) := by
  cases σ
  case ptr q k t =>
    simp only [narrow]
    split
    · rename_i hx
      cases v <;> first
        | exact hc
        | (cases st
           · exact Or.inr rfl
           · exact absurd rfl (hn rfl hx))
    · exact hc
  all_goals exact hc

theorem nullTrap_of_null {R : Rules} {st μ k t} (hS : st = !R.nullTrap)
    (hc : conf st μ .null (.ptr .nonnull k t)) : R.nullTrap = true := by
  rcases hc with h | h
  · cases h
  · cases hR : R.nullTrap <;> simp_all

/-! ## Memory operations -/

theorem rd_safe {st μ b blk} {o : Int} (hW : WFMem st μ) (hb : μ[b]? = some blk) (h0 : 0 ≤ o)
    (h1 : o < blk.cells.length) : Safe (fun v => conf st μ v blk.ty) (rd μ b o) := by
  have hlt : o.toNat < blk.cells.length := by omega
  simp only [rd, hb, ite_eq_left h0, List.getElem?_eq_getElem hlt]
  exact hW b blk hb _ (List.getElem_mem hlt)

theorem lt_of_get {μ : Mem} {b blk} (hb : μ[b]? = some blk) : b < μ.length := by
  rcases Nat.lt_or_ge b μ.length with h | h
  · exact h
  · rw [List.getElem?_eq_none h] at hb; cases hb

theorem wr_safe {st μ b blk} {o : Int} {v : Val} (hW : WFMem st μ) (hb : μ[b]? = some blk)
    (h0 : 0 ≤ o) (h1 : o < blk.cells.length) (hv : conf st μ v blk.ty) :
    Safe (fun μ' => Ext μ μ' ∧ WFMem st μ') (wr μ b o v) := by
  simp only [wr, hb, ite_eq_left (And.intro h0 h1)]
  have hbl := lt_of_get hb
  have hE : Ext μ (μ.set b { blk with cells := blk.cells.set o.toNat v }) := by
    intro b' blk' hb'
    by_cases he : b = b'
    · subst he; rw [hb] at hb'; cases hb'
      exact ⟨_, List.getElem?_set_self hbl, rfl, List.length_set⟩
    · exact ⟨blk', by rw [List.getElem?_set_ne he]; exact hb', rfl, rfl⟩
  refine ⟨hE, ?_⟩
  intro b' blk' hb' w hw
  by_cases he : b = b'
  · subst he
    rw [List.getElem?_set_self hbl] at hb'; cases hb'
    rcases List.mem_or_eq_of_mem_set hw with hw | rfl
    · exact conf_mono hE (hW b blk hb w hw)
    · exact conf_mono hE hv
  · rw [List.getElem?_set_ne he] at hb'
    exact conf_mono hE (hW b' blk' hb' w hw)

theorem addr_safe {R : Rules} {st μ v k t} {i : Int} (hS : st = !R.nullTrap) (hf : R.ntBounded = true)
    (hc : conf st μ v (.ptr .nonnull k t)) :
    Safe (fun a : Nat × Int => ∃ blk, μ[a.1]? = some blk ∧ compat blk.ty t ∧ 0 ≤ a.2 ∧ a.2 < blk.cells.length)
      (addr R false k v i) := by
  cases v with
  | null =>
    have hn := nullTrap_of_null hS hc
    simp [addr, nullAccess, hn, Safe]
  | ptr b off lo hi =>
    obtain ⟨blk, hb, hcomp, hk⟩ := hc
    cases k <;> simp only [kindOK] at hk <;> simp [addr, hf] <;> (try split) <;>
      first | exact True.intro | exact ⟨blk, hb, hcomp, by omega, by omega⟩
  | int _ => exact False.elim hc
  | uninit => exact False.elim hc

theorem step_safe {R : Rules} {st μ vp k t} {n : Int} (hS : st = !R.nullTrap) (hf : R.ntBounded = true)
    (hW : WFMem st μ) (hc : conf st μ vp (.ptr .nonnull k t)) :
    Safe (fun v => conf st μ v (.ptr .nonnull k t)) (step R μ k vp (.int n)) := by
  cases vp with
  | null =>
    have hn := nullTrap_of_null hS hc
    simp [step, nullAccess, hn, Safe]
  | ptr b off lo hi =>
    obtain ⟨blk, hb, hcomp, hk⟩ := hc
    cases k with
    | indexable => exact ⟨blk, hb, hcomp, hk⟩
    | nullterm =>
      simp only [kindOK] at hk
      simp only [step]
      by_cases h1 : n ≠ 1
      · rw [ite_eq_left h1]; exact True.intro
      · rw [ite_eq_right h1]
        by_cases hin : lo ≤ off ∧ off < hi
        · rw [ite_eq_right (by simp [hin])]
          refine Safe.bind (rd_safe hW hb (by omega) (by omega)) fun c _ => ?_
          split
          · exact True.intro
          · exact ⟨blk, hb, hcomp, hk⟩
        · rw [ite_eq_left ⟨hf, hin⟩]; exact True.intro
    | single => exact True.intro
    | counted _ => exact True.intro
  | int _ => exact False.elim hc
  | uninit => exact False.elim hc

theorem findZero_lt {c : List Val} : ∀ {k j r : Nat}, findZero c j k = some r → j ≤ r ∧ r < j + k
  | 0, j, r, h => by simp [findZero] at h
  | k + 1, j, r, h => by
    simp only [findZero] at h
    split at h
    · cases h; omega
    · have := findZero_lt h; omega

theorem convV_safe {R : Rules} {st μ v q k k' t} (hf : R.ntBounded = true) (hc : conf st μ v (.ptr q k t)) :
    Safe (fun v' => conf st μ v' (.ptr q k' t)) (convV R μ k k' v) := by
  cases v with
  | null => exact hc
  | ptr b off lo hi =>
    obtain ⟨blk, hb, hcomp, hk⟩ := hc
    have hidx : ∀ l h, toIdx R k off lo hi = some (l, h) → 0 ≤ l ∧ h ≤ blk.cells.length := by
      intro l h he
      cases k <;> simp only [kindOK] at hk <;> simp [toIdx, hf] at he <;> omega
    simp only [convV]
    split
    · rename_i hkk; subst hkk; exact ⟨blk, hb, hcomp, hk⟩
    · split
      · exact True.intro
      · rename_i l h he
        obtain ⟨hl, hh⟩ := hidx l h he
        cases k' with
        | indexable => exact ⟨blk, hb, hcomp, hl, hh⟩
        | single => dsimp only; split <;> first | exact True.intro | exact ⟨blk, hb, hcomp, by omega, by omega⟩
        | counted n => dsimp only; split <;> first | exact True.intro | exact ⟨blk, hb, hcomp, by omega, by omega⟩
        | nullterm =>
          dsimp only
          split
          · rename_i hr
            simp only [hb, ite_eq_left (And.intro hl hh)]
            split
            · rename_i j hj
              have := findZero_lt hj
              have e1 : ((off.toNat : Nat) : Int) = off := Int.toNat_of_nonneg (by omega)
              have e2 : (((h - off).toNat : Nat) : Int) = h - off := Int.toNat_of_nonneg (by omega)
              exact ⟨blk, hb, hcomp, by omega, by omega⟩
            · exact True.intro
          · exact True.intro
  | int _ => exact False.elim hc
  | uninit => exact False.elim hc

theorem arith_safe {st μ} (op : Op) (x y : Int) : Safe (fun v => conf st μ v .int) (arith false op x y) := by
  cases op <;> simp [arith] <;> (try split) <;> exact True.intro

/-! ## Expressions -/

theorem evalE_safe {R : Rules} (hR : R.Sound) {st : Bool} (hS : st = !R.nullTrap) {D F ρ μ}
    (hI : InvE st D F ρ μ) (hW : WFMem st μ) {e τ} (h : HasTy R false D F e τ) :
    Safe (fun v => conf st μ v τ) (evalE R false ρ μ e) := by
  have hf := hR.2.2
  have hca := hR.2.1
  induction h with
  | lit => exact True.intro
  | null => exact Or.inl rfl
  | @var x σ τ hx hi hτ =>
    subst hτ
    obtain ⟨hc, hn⟩ := hI _ _ hx hi
    have hne : ρ x ≠ .uninit := by intro hu; rw [hu] at hc; cases σ <;> exact hc
    simp only [evalE, ite_eq_right hne]
    exact conf_narrow hc hn
  | bin _ _ hpa hpb iha ihb =>
    simp only [evalE]
    refine Safe.bind iha fun va hva => Safe.bind ihb fun vb hvb => ?_
    obtain ⟨x, rfl⟩ := int_of_pf hpa hva
    obtain ⟨y, rfl⟩ := int_of_pf hpb hvb
    exact arith_safe _ x y
  | index _ _ hpf hnv ihp ihi =>
    simp only [evalE]
    refine Safe.bind ihp fun vp hvp => Safe.bind ihi fun vi hvi => ?_
    obtain ⟨n, rfl⟩ := int_of_pf hpf hvi
    refine Safe.bind (addr_safe hS hf hvp) fun a ha => ?_
    obtain ⟨blk, hb, hcomp, h0, h1⟩ := ha
    exact Safe.mono (rd_safe hW hb h0 h1) fun v hv => conf_of_compat hcomp hnv hv
  | padd _ _ hpf ihp ihi =>
    simp only [evalE]
    refine Safe.bind ihp fun vp hvp => Safe.bind ihi fun vi hvi => ?_
    obtain ⟨n, rfl⟩ := int_of_pf hpf hvi
    exact step_safe hS hf hW hvp
  | conv _ ihp =>
    simp only [evalE]
    exact Safe.bind ihp fun vp hvp => convV_safe hf hvp
  | cast _ hok ihp =>
    simp only [evalE]
    exact Safe.mono ihp fun v hv => conf_cast hca hok hv

theorem storeR_safe {R : Rules} (hR : R.Sound) {st : Bool} (hS : st = !R.nullTrap) {D F ρ μ}
    (hI : InvE st D F ρ μ) (hW : WFMem st μ) {k p i e t τi τ}
    (hp : HasTy R false D F p (.ptr .nonnull k t)) (hi : HasTy R false D F i τi) (hpf : τi.ptrFree = true)
    (hnv : t ≠ .void) (he : HasTy R false D F e τ) (hs : Sub τ t) :
    Safe (fun μ' => Ext μ μ' ∧ WFMem st μ') (storeR R false ρ μ k p i e) := by
  unfold storeR
  refine Safe.bind (evalE_safe hR hS hI hW hp) fun vp hvp =>
    Safe.bind (evalE_safe hR hS hI hW hi) fun vi hvi =>
    Safe.bind (evalE_safe hR hS hI hW he) fun ve hve => ?_
  obtain ⟨n, rfl⟩ := int_of_pf hpf hvi
  refine Safe.bind (addr_safe hS hR.2.2 hvp) fun a ha => ?_
  obtain ⟨blk, hb, hcomp, h0, h1⟩ := ha
  exact wr_safe hW hb h0 h1 (conf_to_compat hcomp hnv (conf_sub hs hve))

theorem alloc_inv {R : Rules} (hR : R.Sound) {st : Bool} (hS : st = !R.nullTrap) {D F ρ μ}
    (hI : InvE st D F ρ μ) (hW : WFMem st μ) {x q t} {blk : Block}
    (hx : D.lookup x = some (.ptr q .indexable t)) (hnv : t ≠ .void)
    (hz : R.zeroNonnull = true ∨ t.isNonnullPtr = false)
    (hty : blk.ty = t) (hcells : ∀ v ∈ blk.cells, v = zeroOf t) :
    InvE st D (F.assign x) (ρ.set x (.ptr μ.length 0 0 blk.cells.length)) (μ ++ [blk]) ∧
      WFMem st (μ ++ [blk]) := by
  have hE : Ext μ (μ ++ [blk]) := fun b blk' hb =>
    ⟨blk', by rw [List.getElem?_append_left (lt_of_get hb)]; exact hb, rfl, rfl⟩
  have hnew : (μ ++ [blk])[μ.length]? = some blk := by simp
  have hz' : conf st (μ ++ [blk]) (zeroOf t) t := by
    cases t with
    | ptr q' k' t' =>
      cases q' with
      | nullable => exact Or.inl rfl
      | nonnull =>
        right
        rcases hR.1 with h | ⟨h1, _⟩
        · simp [hS, h]
        · rcases hz with h | h
          · rw [h1] at h; cases h
          · simp [Ty.isNonnullPtr] at h
    | int => exact True.intro
    | char => exact True.intro
    | void => exact absurd rfl hnv
  refine ⟨InvE_assign (InvE_mono hE hI) hx ⟨blk, hnew, Or.inr (Or.inl hty), by simp [kindOK]⟩, ?_⟩
  intro b blk' hb v hv
  rcases Nat.lt_or_ge b μ.length with h | h
  · rw [List.getElem?_append_left h] at hb
    exact conf_mono hE (hW b blk' hb v hv)
  · have : b = μ.length := by
      rcases Nat.lt_or_ge b (μ.length + 1) with h' | h'
      · omega
      · rw [List.getElem?_eq_none (by simp; omega)] at hb; cases hb
    subst this
    rw [hnew] at hb; cases hb
    rw [hcells v hv, hty]
    exact hz'

/-! ## Statements -/

theorem exec_safe {R : Rules} (hR : R.Sound) {st : Bool} (hS : st = !R.nullTrap) {D : Decls} :
    ∀ n u F s G ρ μ, HasTyS R D u F s G → u = false → s.noUnsafe = true → InvE st D F ρ μ →
      WFMem st μ → SafeO (fun ρ' μ' => InvE st D G ρ' μ' ∧ WFMem st μ') (exec R false n ρ μ s) := by
  intro n
  induction n with
  | zero => intro _ _ _ _ _ _ _ _ _ _ _; exact True.intro
  | succ n ih =>
    intro u F s G ρ μ hT
    induction hT generalizing ρ μ with
    | skip => intro _ _ hI hW; exact ⟨hI, hW⟩
    | seq ha hb _ _ =>
      intro hu hn hI hW; subst hu
      simp only [Stmt.noUnsafe, Bool.and_eq_true] at hn
      simp only [exec]
      exact SafeO_othen (ih _ _ _ _ ρ μ ha rfl hn.1 hI hW) fun ρ1 μ1 h1 =>
        ih _ _ _ _ ρ1 μ1 hb rfl hn.2 h1.1 h1.2
    | assign hx he hs =>
      intro hu _ hI hW; subst hu
      simp only [exec]
      exact SafeO_rthen (evalE_safe hR hS hI hW he) fun v hv => ⟨InvE_assign hI hx (conf_sub hs hv), hW⟩
    | store hp hi hpf hnv he hs =>
      intro hu _ hI hW; subst hu
      simp only [exec]
      exact SafeO_rthen (storeR_safe hR hS hI hW hp hi hpf hnv he hs) fun μ' h => ⟨InvE_mono h.1 hI, h.2⟩
    | @alloc x q t u F m hx hnv hz =>
      intro _ _ hI hW
      have := alloc_inv (blk := ⟨t, List.replicate m (zeroOf t)⟩) hR hS hI hW hx hnv hz rfl
        (fun v hv => (List.mem_replicate.1 hv).2)
      simp only [List.length_replicate] at this
      exact this
    | ifz hc hpf ha hb _ _ =>
      intro hu hn hI hW; subst hu
      simp only [Stmt.noUnsafe, Bool.and_eq_true] at hn
      simp only [exec]
      refine SafeO_rthen (evalE_safe hR hS hI hW hc) fun v hv => ?_
      obtain ⟨z, rfl⟩ := int_of_pf hpf hv
      dsimp only
      split
      · exact SafeO_mono (ih _ _ _ _ ρ μ ha rfl hn.1 hI hW) fun _ _ h =>
          ⟨InvE_weaken h.1 meet_sub.1 meet_sub.2.1, h.2⟩
      · exact SafeO_mono (ih _ _ _ _ ρ μ hb rfl hn.2 hI hW) fun _ _ h =>
          ⟨InvE_weaken h.1 meet_sub.2.2.1 meet_sub.2.2.2, h.2⟩
    | ifNull hx hi ha hb _ _ =>
      intro hu hn hI hW; subst hu
      simp only [Stmt.noUnsafe, Bool.and_eq_true] at hn
      simp only [exec]
      split
      · exact SafeO_mono (ih _ _ _ _ ρ μ ha rfl hn.1 hI hW) fun _ _ h =>
          ⟨InvE_weaken h.1 meet_sub.1 meet_sub.2.1, h.2⟩
      · rename_i hne
        exact SafeO_mono (ih _ _ _ _ ρ μ hb rfl hn.2 (InvE_narrow hI hne) hW) fun _ _ h =>
          ⟨InvE_weaken h.1 meet_sub.2.2.1 meet_sub.2.2.2, h.2⟩
    | loop hc hpf hb h1 h2 _ =>
      intro hu hn hI hW; subst hu
      simp only [Stmt.noUnsafe] at hn
      simp only [exec]
      refine SafeO_rthen (evalE_safe hR hS hI hW hc) fun v hv => ?_
      obtain ⟨z, rfl⟩ := int_of_pf hpf hv
      dsimp only
      split
      · exact ⟨hI, hW⟩
      · exact SafeO_othen (ih _ _ _ _ ρ μ hb rfl hn hI hW) fun ρ1 μ1 hb1 =>
          ih _ _ _ _ ρ1 μ1 (HasTyS.loop hc hpf hb h1 h2) rfl hn (InvE_weaken hb1.1 h1 h2) hb1.2
    | unsafeBlk _ _ =>
      intro _ hn
      simp [Stmt.noUnsafe] at hn
    | call hb hsub _ =>
      intro hu hn hI hW; subst hu
      simp only [Stmt.noUnsafe] at hn
      simp only [exec]
      have hIc := InvE_weaken (G := ⟨_, []⟩) hI (fun _ h => h) (fun _ h => by cases h)
      refine SafeO_mono (ih _ _ _ _ ρ μ hb rfl hn hIc hW) fun ρ' μ' h => ⟨?_, h.2⟩
      split
      · rename_i hk
        have hst : st = false := by
          rcases hR.1 with h0 | ⟨_, h0⟩
          · rw [hS, h0]; rfl
          · rw [h0] at hk; cases hk
        exact InvE_init_only hst h.1 hsub
      · exact InvE_weaken h.1 hsub (fun _ h => by cases h)
    | weaken _ h1 h2 ih' =>
      intro hu hn hI hW
      exact SafeO_mono (ih' ρ μ hu hn hI hW) fun _ _ h => ⟨InvE_weaken h.1 h1 h2, h.2⟩

/-! ## Main theorem -/

/-- A well-typed Safe C program with no `__unsafe` block, run from empty
memory with every variable uninitialized, never reaches UB: for every fuel
bound it finishes, traps, or runs out of fuel (diverges). -/
theorem safe_c_sound (R : Rules) (hR : R.Sound) (D : Decls) (s : Stmt) (G : Facts)
    (hT : HasTyS R D false Facts.empty s G) (hu : s.noUnsafe = true) (fuel : Nat) :
    exec R false fuel (fun _ => .uninit) [] s ≠ .ub := by
  intro h
  have := exec_safe hR rfl fuel false _ s G (fun _ => .uninit) [] hT rfl hu InvE_empty WFMem_nil
  rw [h] at this
  exact this

theorem safe_c_sound' (R : Rules) (hR : R.Sound) (D : Decls) (s : Stmt) (G : Facts)
    (hT : HasTyS R D false Facts.empty s G) (hu : s.noUnsafe = true) (fuel : Nat) :
    (∃ ρ μ, exec R false fuel (fun _ => .uninit) [] s = .ok ρ μ) ∨
      exec R false fuel (fun _ => .uninit) [] s = .trap ∨
      exec R false fuel (fun _ => .uninit) [] s = .outOfFuel := by
  have := safe_c_sound R hR D s G hT hu fuel
  cases h : exec R false fuel (fun _ => .uninit) [] s with
  | ok ρ μ => exact Or.inl ⟨ρ, μ, rfl⟩
  | trap => exact Or.inr (Or.inl rfl)
  | ub => exact absurd h this
  | outOfFuel => exact Or.inr (Or.inr rfl)

end SafeC
