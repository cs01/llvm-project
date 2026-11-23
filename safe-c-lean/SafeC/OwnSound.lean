-- Soundness of the ownership rules: a program that passes `Prog.ok` and
-- `chkS` under `Rules.full` never reaches UB, so in particular never a use
-- after free or a double free, for any amount of fuel.
import SafeC.Own
import SafeC.OwnHoles

namespace SafeC.Own

/-! ## Ghost ownership

The proof tracks, for every block, who owns it: a variable of some call frame
(`var (depth, x)`), an owned field of another block, or nobody. A block's
region is the variable at the top of its chain of owners (`Anc`). Owner
chains are never stored in the program state; they are existential in the
invariant. -/

/-- A region: (frame depth, owner variable). -/
abbrev Lbl := Nat × Nat

inductive Par where
  | nil
  | var (l : Lbl)
  | fld (b k f : Nat)
  deriving DecidableEq

abbrev PMap := Nat → Par

inductive Anc (par : PMap) : Nat → Lbl → Prop where
  | var {b l} : par b = .var l → Anc par b l
  | fld {b b' k f l} : par b = .fld b' k f → Anc par b' l → Anc par b l

theorem Anc.det {par : PMap} {b l1 l2} (h1 : Anc par b l1) (h2 : Anc par b l2) : l1 = l2 := by
  induction h1 generalizing l2 with
  | var h => cases h2 with
    | var h' => rw [h] at h'; cases h'; rfl
    | fld h' _ => rw [h] at h'; cases h'
  | fld h _ ih => cases h2 with
    | var h' => rw [h] at h'; cases h'
    | fld h' h2' => rw [h] at h'; cases h'; exact ih h2'

theorem Anc.ne_nil {par : PMap} {b l} (h : Anc par b l) : par b ≠ .nil := by
  cases h with
  | var h => rw [h]; simp
  | fld h _ => rw [h]; simp

/-- Changing owners only at blocks whose region is in `K` keeps every region
outside `K` as it was. -/
theorem anc_keep {par par' : PMap} {K : Lbl → Prop}
    (hch : ∀ b l, par' b ≠ par b → Anc par b l → K l) {b l} (h : Anc par b l) (hk : ¬ K l) :
    Anc par' b l := by
  induction h with
  | @var b l h0 =>
    by_cases e : par' b = par b
    · exact .var (e ▸ h0)
    · exact absurd (hch _ _ e (.var h0)) hk
  | @fld b b' k f l h0 ha ih =>
    by_cases e : par' b = par b
    · exact .fld (e ▸ h0) (ih hk)
    · exact absurd (hch _ _ e (.fld h0 ha)) hk

/-- Every block with a region after the change had one before, or is new. -/
theorem anc_coarse {par par' : PMap} {N : Nat → Prop}
    (hch : ∀ b, par' b ≠ par b → par' b ≠ .nil → (∃ l, Anc par b l) ∨ N b)
    (hun : ∀ b b' k f, par b = .fld b' k f → N b' → False) {b l} (h : Anc par' b l) :
    (∃ l', Anc par b l') ∨ N b := by
  induction h with
  | @var b l h0 =>
    by_cases e : par' b = par b
    · exact .inl ⟨l, .var (e ▸ h0)⟩
    · exact hch b e (by rw [h0]; simp)
  | @fld b b' k f l h0 ha ih =>
    by_cases e : par' b = par b
    · have hp : par b = .fld b' k f := e ▸ h0
      rcases ih with ⟨l', ha'⟩ | hn
      · exact .inl ⟨l', .fld hp ha'⟩
      · exact (hun _ _ _ _ hp hn).elim
    · exact hch b e (by rw [h0]; simp)

/-- Owners at blocks whose region is shallower than `d` are untouched. -/
def Frame (d : Nat) (par par' : PMap) : Prop := ∀ b l, Anc par b l → l.1 < d → par' b = par b

theorem Frame.anc {d} {par par' : PMap} (hF : Frame d par par') {b l} (h : Anc par b l)
    (hl : l.1 < d) : Anc par' b l :=
  anc_keep (K := fun l => ¬ l.1 < d) (fun b l e ha hl => e (hF b l ha hl)) h (by simpa using hl)

theorem Frame.refl {d} {par : PMap} : Frame d par par := fun _ _ _ _ => rfl

theorem Frame.trans {d} {p1 p2 p3 : PMap} (h1 : Frame d p1 p2) (h2 : Frame d p2 p3) : Frame d p1 p3 :=
  fun b l ha hl => (h2 b l (h1.anc ha hl) hl).trans (h1 b l ha hl)

theorem frame_of {d} {par par' : PMap} (hch : ∀ b l, par' b ≠ par b → Anc par b l → l.1 = d) :
    Frame d par par' := fun b l ha hl => by
  apply Classical.byContradiction; intro e; have := hch b l e ha; omega

/-! ## The invariant -/

def sidOf (μ : Mem) (b : Nat) : Option Nat := (μ[b]?).map (·.sid)

def fields (P : Prog) (s : Nat) : List FTy := (P.structs[s]?).getD []

theorem fieldTy_eq (P : Prog) (s f : Nat) : fieldTy P s f = (fields P s)[f]? := by
  unfold fieldTy fields; cases P.structs[s]? <;> simp

def FOK (μ : Mem) (par : PMap) (b k f : Nat) : FTy → V → Prop
  | .int, v => ∃ n, v = .int n
  | .own s, v => v = .null ∨ ∃ c, v = .ptr c 0 ∧ par c = .fld b k f ∧ sidOf μ c = some s

/-- A block in some region: live, every element shaped like its struct, every
owned field pointing at a block that names this field as its owner. -/
def BlkOK (P : Prog) (μ : Mem) (par : PMap) (b : Nat) (blk : Blk) : Prop :=
  blk.live = true ∧ ∀ k el, blk.cells[k]? = some el →
    el.length = (fields P blk.sid).length ∧
    ∀ f t v, (fields P blk.sid)[f]? = some t → el[f]? = some v → FOK μ par b k f t v

def lab (d : Nat) (θ : Nat → Lbl) : Root → Lbl
  | .loc y => (d, y)
  | .ext p => θ p

def VOK (D : Decls) (d : Nat) (θ : Nat → Lbl) (roots : List (Nat × Root)) (ρ : Env) (μ : Mem)
    (par : PMap) (x : Nat) : Prop :=
  match D.lookup x with
  | some .int => ∃ n, ρ x = .int n
  | some (.own s) => ρ x = .null ∨ ∃ b, ρ x = .ptr b 0 ∧ par b = .var (d, x) ∧ sidOf μ b = some s
  | some (.ref s) => ρ x = .null ∨ ∃ b off r, ρ x = .ptr b off ∧ sidOf μ b = some s ∧
      roots.lookup x = some r ∧ Anc par b (lab d θ r)
  | none => True

/-- `d` is the call depth, `θ` maps each `ext p` root to the caller's region. -/
structure Inv (P : Prog) (D : Decls) (d : Nat) (θ : Nat → Lbl) (Γ : Ctx) (ρ : Env) (μ : Mem)
    (par : PMap) : Prop where
  theta : ∀ p, (θ p).1 < d
  dom : ∀ c b k f, par c = .fld b k f → b < μ.length
  heap : ∀ b l, Anc par b l → ∃ blk, μ[b]? = some blk ∧ BlkOK P μ par b blk
  vars : ∀ x, x ∈ Γ.live → VOK D d θ Γ.roots ρ μ par x

def SidPres (μ μ' : Mem) : Prop := ∀ c s, sidOf μ c = some s → sidOf μ' c = some s

theorem SidPres.refl {μ : Mem} : SidPres μ μ := fun _ _ h => h
theorem SidPres.trans {μ1 μ2 μ3 : Mem} (h1 : SidPres μ1 μ2) (h2 : SidPres μ2 μ3) : SidPres μ1 μ3 :=
  fun c s h => h2 c s (h1 c s h)

def Good (P : Prog) (D : Decls) (d : Nat) (θ : Nat → Lbl) (μ : Mem) (par : PMap) (Γ' : Ctx) :
    Out → Prop
  | .ok ρ' μ' => ∃ par', Inv P D d θ Γ' ρ' μ' par' ∧ Frame d par par' ∧ SidPres μ μ'
  | .ub _ => False
  | _ => True


/-! ## Memory plumbing -/

theorem sidOf_some {μ : Mem} {b s} (h : sidOf μ b = some s) : ∃ blk, μ[b]? = some blk ∧ blk.sid = s := by
  unfold sidOf at h; cases e : μ[b]? <;> simp_all

theorem sidOf_lt {μ : Mem} {b s} (h : sidOf μ b = some s) : b < μ.length := by
  obtain ⟨blk, hb, -⟩ := sidOf_some h
  exact (List.getElem?_eq_some_iff.mp hb).1

theorem sidPres_set {μ : Mem} {b : Nat} {blk blk' : Blk} (hb : μ[b]? = some blk) (hs : blk'.sid = blk.sid) :
    SidPres μ (μ.set b blk') := by
  intro c s h
  unfold sidOf at *
  rw [List.getElem?_set]
  by_cases e : b = c
  · subst e; rw [hb] at h; simp at h; simp [(List.getElem?_eq_some_iff.mp hb).1, hs, h]
  · simp [e, h]

theorem sidPres_app {μ : Mem} {nb : Blk} : SidPres μ (μ ++ [nb]) := by
  intro c s h
  have hl := sidOf_lt h
  unfold sidOf at *
  rw [List.getElem?_append_left hl]; exact h

theorem getElem?_app_len {μ : Mem} {nb : Blk} : (μ ++ [nb])[μ.length]? = some nb := by simp

theorem setc_eq {μ : Mem} {b k f : Nat} {v : V} {blk : Blk} {el : List V} (hb : μ[b]? = some blk)
    (hk : blk.cells[k]? = some el) :
    setc μ b k f v = μ.set b { blk with cells := blk.cells.set k (el.set f v) } := by
  unfold setc; simp [hb, hk]

theorem getc_eq {μ : Mem} {b k f : Nat} {blk : Blk} {el : List V} (hb : μ[b]? = some blk)
    (hk : blk.cells[k]? = some el) : getc μ b k f = el[f]? := by
  unfold getc; simp [hb, hk]

theorem FOK.mono {μ μ' : Mem} {par par' : PMap} {b k f t v} (h : FOK μ par b k f t v) (hs : SidPres μ μ')
    (hp : ∀ c, c < μ.length → par c = .fld b k f → par' c = .fld b k f) : FOK μ' par' b k f t v := by
  cases t with
  | int => exact h
  | own s =>
    rcases h with h | ⟨c, h1, h2, h3⟩
    · exact .inl h
    · exact .inr ⟨c, h1, hp c (sidOf_lt h3) h2, hs c s h3⟩

theorem BlkOK.mono {P : Prog} {μ μ' : Mem} {par par' : PMap} {b blk} (h : BlkOK P μ par b blk)
    (hs : SidPres μ μ') (hp : ∀ c k f, c < μ.length → par c = .fld b k f → par' c = .fld b k f) :
    BlkOK P μ' par' b blk := by
  refine ⟨h.1, fun k el hk => ⟨(h.2 k el hk).1, fun f t v ht hv => ?_⟩⟩
  exact ((h.2 k el hk).2 f t v ht hv).mono hs (fun c hl hc => hp c k f hl hc)

theorem BlkOK.setc {P : Prog} {μ μ' : Mem} {par par' : PMap} {b blk k f v} {el : List V}
    (h : BlkOK P μ par b blk) (hk : blk.cells[k]? = some el) (hs : SidPres μ μ')
    (hp : ∀ c k' f', (k' = k → f' ≠ f) → c < μ.length → par c = .fld b k' f' → par' c = .fld b k' f')
    (hv : ∀ t, (fields P blk.sid)[f]? = some t → FOK μ' par' b k f t v) :
    BlkOK P μ' par' b { blk with cells := blk.cells.set k (el.set f v) } := by
  refine ⟨h.1, fun k2 el2 hk2 => ?_⟩
  simp only [List.getElem?_set] at hk2
  by_cases e : k = k2
  · subst e
    have hlt := (List.getElem?_eq_some_iff.mp hk).1
    simp [hlt] at hk2; subst hk2
    obtain ⟨hl, hf⟩ := h.2 k el hk
    refine ⟨by simp [hl], fun f2 t v2 ht hv2 => ?_⟩
    simp only [List.getElem?_set] at hv2
    by_cases e2 : f = f2
    · subst e2
      have : v2 = v := by
        by_cases hf2 : f < el.length
        · simp [hf2] at hv2; exact hv2.symm
        · simp [hf2] at hv2
      subst this; exact hv t ht
    · simp [e2] at hv2
      exact (hf f2 t v2 ht hv2).mono hs (fun c hl hc => hp c k f2 (fun _ => Ne.symm e2) hl hc)
  · simp [e] at hk2
    obtain ⟨hl, hf⟩ := h.2 k2 el2 hk2
    exact ⟨hl, fun f2 t v2 ht hv2 => (hf f2 t v2 ht hv2).mono hs
      (fun c hl hc => hp c k2 f2 (fun h => absurd h.symm e) hl hc)⟩

/-! ## Contexts -/

theorem mem_kill {Γ : Ctx} {r y} : y ∈ (Γ.kill r).live ↔ y ∈ Γ.live ∧ Γ.roots.lookup y ≠ some r := by
  simp [Ctx.kill]

@[simp] theorem kill_roots {Γ : Ctx} {r} : (Γ.kill r).roots = Γ.roots := rfl
@[simp] theorem add_roots {Γ : Ctx} {x} : (Γ.add x).roots = Γ.roots := rfl
@[simp] theorem drop_roots {Γ : Ctx} {x} : (Γ.drop x).roots = Γ.roots := rfl
@[simp] theorem assignOwn_roots {Γ : Ctx} {x} : (Γ.assignOwn x).roots = Γ.roots := rfl
theorem mem_add {Γ : Ctx} {x y} : y ∈ (Γ.add x).live ↔ y = x ∨ y ∈ Γ.live := by simp [Ctx.add]
theorem mem_drop {Γ : Ctx} {x y} : y ∈ (Γ.drop x).live ↔ y ∈ Γ.live ∧ y ≠ x := by simp [Ctx.drop]
theorem mem_addRef {Γ : Ctx} {x y r} : y ∈ (Γ.addRef x r).live ↔ y = x ∨ y ∈ Γ.live := by simp [Ctx.addRef]
theorem addRef_lookup {Γ : Ctx} {x y r} :
    (Γ.addRef x r).roots.lookup y = if y = x then some r else Γ.roots.lookup y := by
  simp [Ctx.addRef, List.lookup]
  by_cases e : y = x
  · simp [e]
  · have : (y == x) = false := by simpa using e
    simp [this, e]
theorem mem_assignOwn {Γ : Ctx} {x y} :
    y ∈ (Γ.assignOwn x).live ↔ y = x ∨ (y ∈ Γ.live ∧ Γ.roots.lookup y ≠ some (.loc x)) := by
  simp [Ctx.assignOwn, mem_add, mem_kill]

theorem VOK.roots {D d θ ρ μ par x} {r r' : List (Nat × Root)}
    (h : VOK D d θ r ρ μ par x) (hr : isRef D x = true → r'.lookup x = r.lookup x) :
    VOK D d θ r' ρ μ par x := by
  unfold VOK at *
  unfold isRef at hr
  split at h <;> rename_i hD <;> simp only [hD] at hr ⊢
  all_goals first | exact h | skip
  rcases h with h | ⟨b, off, rt, h1, h2, h3, h4⟩
  · exact .inl h
  · exact .inr ⟨b, off, rt, h1, h2, by rw [hr trivial]; exact h3, h4⟩

theorem Inv.weaken {P D d θ ρ μ par} {Γ Γ' : Ctx} (h : Inv P D d θ Γ ρ μ par)
    (hw : ∀ x ∈ Γ'.live, x ∈ Γ.live ∧ (isRef D x = true → Γ'.roots.lookup x = Γ.roots.lookup x)) :
    Inv P D d θ Γ' ρ μ par :=
  ⟨h.theta, h.dom, h.heap, fun x hx => (h.vars x (hw x hx).1).roots (hw x hx).2⟩

theorem Inv.meet_l {P D d θ ρ μ par} {F G : Ctx} (h : Inv P D d θ F ρ μ par) :
    Inv P D d θ (meet D F G) ρ μ par :=
  h.weaken fun x hx => by simp [meet] at hx; exact ⟨hx.1, fun _ => rfl⟩

theorem Inv.meet_r {P D d θ ρ μ par} {F G : Ctx} (h : Inv P D d θ G ρ μ par) :
    Inv P D d θ (meet D F G) ρ μ par :=
  h.weaken fun x hx => by
    simp [meet] at hx
    exact ⟨hx.2.1, fun hr => by have := hx.2.2; simp [hr] at this; simpa [meet] using this⟩

theorem Inv.sub {P D d θ ρ μ par} {F G : Ctx} (h : Inv P D d θ G ρ μ par) (hs : sub D F G = true) :
    Inv P D d θ F ρ μ par :=
  h.weaken fun x hx => by
    simp only [SafeC.Own.sub, List.all_eq_true, Bool.and_eq_true, decide_eq_true_eq] at hs
    have := hs x hx
    exact ⟨this.1, fun hr => by have := this.2; simp [hr] at this; exact this.symm⟩

/-! ## Expressions -/

section
variable {P : Prog} {D : Decls} {d : Nat} {θ : Nat → Lbl} {Γ : Ctx} {ρ : Env} {μ : Mem} {par : PMap}

theorem live_ptr (hI : Inv P D d θ Γ ρ μ par) {y s} (hy : y ∈ Γ.live) (hs : ptrS D y = some s) :
    ρ y = .null ∨ ∃ b off r, ρ y = .ptr b off ∧ sidOf μ b = some s ∧ rootOf D Γ y = some r ∧
      Anc par b (lab d θ r) ∧ (isOwn D y = true → off = 0 ∧ par b = .var (d, y)) := by
  have hv := hI.vars y hy
  unfold VOK at hv; unfold ptrS at hs; unfold rootOf isOwn
  split at hs <;> rename_i hD <;> simp only [hD] at hv ⊢ <;> cases hs
  · rcases hv with h | ⟨b, h1, h2, h3⟩
    · exact .inl h
    · exact .inr ⟨b, 0, .loc y, h1, h3, rfl, .var h2, fun _ => ⟨rfl, h2⟩⟩
  · rcases hv with h | ⟨b, off, r, h1, h2, h3, h4⟩
    · exact .inl h
    · exact .inr ⟨b, off, r, h1, h2, h3, h4, fun h => by simp at h⟩

theorem elem_ok (hI : Inv P D d θ Γ ρ μ par) {b l} (ha : Anc par b l) (off i : Int) :
    elemAt μ (.ptr b off) i = .trap ∨ ∃ blk k el, elemAt μ (.ptr b off) i = .ok (b, k) ∧
      μ[b]? = some blk ∧ BlkOK P μ par b blk ∧ blk.cells[k]? = some el := by
  obtain ⟨blk, hb, hok⟩ := hI.heap b l ha
  unfold elemAt; simp only [hb, hok.1, ite_true]
  split
  · rename_i h
    have hk : (off + i).toNat < blk.cells.length := by omega
    exact .inr ⟨blk, _, _, rfl, by first | exact hb | rfl, hok, List.getElem?_eq_getElem hk⟩
  · exact .inl rfl

theorem ptr_elem (hI : Inv P D d θ Γ ρ μ par) {y s} (hy : y ∈ Γ.live) (hs : ptrS D y = some s) (n : Int) :
    elemAt μ (ρ y) n = .trap ∨ ∃ b off r blk k el, elemAt μ (ρ y) n = .ok (b, k) ∧ ρ y = .ptr b off ∧
      rootOf D Γ y = some r ∧ Anc par b (lab d θ r) ∧ μ[b]? = some blk ∧ blk.sid = s ∧
      BlkOK P μ par b blk ∧ blk.cells[k]? = some el ∧ (isOwn D y = true → off = 0 ∧ par b = .var (d, y)) := by
  rcases live_ptr hI hy hs with h | ⟨b, off, r, h1, h2, h3, h4, h5⟩
  · rw [h]; exact .inl rfl
  · rw [h1]
    rcases elem_ok hI h4 off n with e | ⟨blk, k, el, e, hb, hok, hk⟩
    · exact .inl e
    · obtain ⟨blk', hb', hsid⟩ := sidOf_some h2
      rw [hb] at hb'; cases hb'
      exact .inr ⟨b, off, r, blk, k, el, e, rfl, h3, h4, hb, hsid, hok, hk, h5⟩

theorem rd_ok {b blk k el f t} (hok : BlkOK P μ par b blk) (hb : μ[b]? = some blk)
    (hk : blk.cells[k]? = some el) (ht : fieldTy P blk.sid f = some t) :
    ∃ v, getc μ b k f = some v ∧ rdF μ b k f = .ok v ∧ FOK μ par b k f t v := by
  rw [fieldTy_eq] at ht
  obtain ⟨hl, hf⟩ := hok.2 k el hk
  have hlt : f < el.length := by rw [hl]; exact (List.getElem?_eq_some_iff.mp ht).1
  have hv : el[f]? = some el[f] := List.getElem?_eq_getElem hlt
  have hg : getc μ b k f = some el[f] := by rw [getc_eq hb hk, hv]
  exact ⟨el[f], hg, by unfold rdF; rw [hg], hf f t _ ht hv⟩

theorem arith_ok (op : Op) (x y : Int) : arithV op x y = .trap ∨ ∃ n, arithV op x y = .ok (.int n) := by
  cases op <;> simp only [arithV]
  all_goals first | exact .inr ⟨_, rfl⟩ | (split; exact .inl rfl; exact .inr ⟨_, rfl⟩)

theorem evalE_ok (hI : Inv P D d θ Γ ρ μ par) :
    ∀ e, chkE P D Γ e = true → evalE ρ μ e = .trap ∨ ∃ n, evalE ρ μ e = .ok (.int n) := by
  intro e he
  induction e with
  | lit n => exact .inr ⟨n, rfl⟩
  | var x =>
    simp only [chkE, Bool.and_eq_true, decide_eq_true_eq] at he
    have hv := hI.vars x he.1
    unfold VOK at hv; rw [he.2] at hv
    obtain ⟨n, hn⟩ := hv
    exact .inr ⟨n, by simp [evalE, hn]⟩
  | bin op a b iha ihb =>
    simp only [chkE, Bool.and_eq_true] at he
    rcases iha he.1 with ha | ⟨na, ha⟩
    · exact .inl (by simp [evalE, ha, Res.bind])
    rcases ihb he.2 with hb | ⟨nb, hb⟩
    · exact .inl (by simp [evalE, ha, hb, Res.bind])
    simp only [evalE, ha, hb, Res.bind]
    exact arith_ok op na nb
  | ld y i f ihi =>
    simp only [chkE, Bool.and_eq_true, decide_eq_true_eq] at he
    obtain ⟨⟨hy, hi⟩, hf⟩ := he
    cases hs : ptrS D y with
    | none => simp [hs] at hf
    | some s =>
      simp only [hs, decide_eq_true_eq] at hf
      rcases ihi hi with h | ⟨n, h⟩
      · exact .inl (by simp [evalE, h, Res.bind])
      rcases ptr_elem hI hy hs n with e | ⟨b, off, r, blk, k, el, e, -, -, -, hb, hsid, hok, hk, -⟩
      · exact .inl (by simp [evalE, h, e, Res.bind])
      · subst hsid
        obtain ⟨v, -, hr, hv⟩ := rd_ok hok hb hk hf
        obtain ⟨m, hm⟩ := hv
        exact .inr ⟨m, by simp [evalE, h, e, Res.bind, hr, hm]⟩
  | nz y =>
    simp only [chkE, Bool.and_eq_true, decide_eq_true_eq] at he
    cases hs : ptrS D y with
    | none => simp [hs] at he
    | some s =>
      rcases live_ptr hI he.1 hs with h | ⟨b, off, -, h, -⟩
      · exact .inr ⟨0, by simp [evalE, h]⟩
      · exact .inr ⟨1, by simp [evalE, h]⟩

theorem fieldAt_ok (hI : Inv P D d θ Γ ρ μ par) {y s i} (hy : y ∈ Γ.live) (hs : ptrS D y = some s)
    (hi : chkE P D Γ i = true) :
    fieldAt ρ μ y i = .trap ∨ ∃ b off r blk k el, fieldAt ρ μ y i = .ok (b, k) ∧ ρ y = .ptr b off ∧
      rootOf D Γ y = some r ∧ Anc par b (lab d θ r) ∧ μ[b]? = some blk ∧ blk.sid = s ∧
      BlkOK P μ par b blk ∧ blk.cells[k]? = some el ∧ (isOwn D y = true → off = 0 ∧ par b = .var (d, y)) := by
  rcases evalE_ok hI i hi with h | ⟨n, h⟩
  · exact .inl (by simp [fieldAt, h, Res.bind])
  · have : fieldAt ρ μ y i = elemAt μ (ρ y) n := by simp [fieldAt, h, Res.bind]
    rw [this]; exact ptr_elem hI hy hs n

end

/-! ## Updating the invariant -/

def upd (par : PMap) (b : Nat) (p : Par) : PMap := fun b' => if b' = b then p else par b'

/-- Changed owners at depth `d` variables `ks` only. -/
def KS (d : Nat) (ks : List Nat) (l : Lbl) : Prop := l.1 = d ∧ l.2 ∈ ks

theorem frame_KS {d ks} {par par' : PMap} (hch : ∀ b l, par' b ≠ par b → Anc par b l → KS d ks l) :
    Frame d par par' := frame_of fun b l e ha => (hch b l e ha).1

theorem lab_notKS {d θ ks r} (hθ : ∀ p, (θ p).1 < d) (hr : ∀ k ∈ ks, r ≠ Root.loc k) :
    ¬ KS d ks (lab d θ r) := by
  cases r with
  | loc y => intro h; exact hr y h.2 rfl
  | ext p => intro h; have := hθ p; simp [lab, KS] at h; omega

section
variable {P : Prog} {D : Decls} {d : Nat} {θ : Nat → Lbl}

theorem VOK.keep {r r' : List (Nat × Root)} {ρ ρ' : Env} {μ μ' : Mem} {par par' : PMap} {K : Lbl → Prop} {y}
    (h : VOK D d θ r ρ μ par y) (hch : ∀ b l, par' b ≠ par b → Anc par b l → K l)
    (he : ρ' y = ρ y) (hs : SidPres μ μ') (hr : isRef D y = true → r'.lookup y = r.lookup y)
    (hko : isOwn D y = true → ∀ b, ρ y = .ptr b 0 → par b = .var (d, y) → par' b = par b)
    (hkr : isRef D y = true → ∀ rt, r.lookup y = some rt → ¬ K (lab d θ rt)) :
    VOK D d θ r' ρ' μ' par' y := by
  unfold VOK at *; unfold isOwn at hko; unfold isRef at hr hkr
  rw [he]
  split at h <;> rename_i hD <;> simp only [hD] at hko hr hkr ⊢
  · exact h
  · rcases h with h | ⟨b, h1, h2, h3⟩
    · exact .inl h
    · exact .inr ⟨b, h1, by rw [hko trivial b h1 h2]; exact h2, hs _ _ h3⟩
  · rcases h with h | ⟨b, off, rt, h1, h2, h3, h4⟩
    · exact .inl h
    · exact .inr ⟨b, off, rt, h1, hs _ _ h2, by rw [hr trivial]; exact h3,
        anc_keep hch h4 (hkr trivial rt h3)⟩

/-- The heap part of the invariant after a change of memory and owners. `N`
are new blocks, `M` the blocks whose contents changed. -/
theorem heap_of {μ μ' : Mem} {par par' : PMap} {N M : Nat → Prop}
    (hH : ∀ b l, Anc par b l → ∃ blk, μ[b]? = some blk ∧ BlkOK P μ par b blk)
    (hco : ∀ b l, Anc par' b l → (∃ l', Anc par b l') ∨ N b)
    (hnew : ∀ b l, N b → Anc par' b l → ∃ blk, μ'[b]? = some blk ∧ BlkOK P μ' par' b blk)
    (hmod : ∀ b blk l l', M b → μ[b]? = some blk → BlkOK P μ par b blk → Anc par b l' → Anc par' b l →
      ∃ blk', μ'[b]? = some blk' ∧ BlkOK P μ' par' b blk')
    (hsame : ∀ b l, ¬ M b → Anc par b l → μ'[b]? = μ[b]?)
    (hkids : ∀ b l c k f, ¬ M b → Anc par b l → c < μ.length → par c = .fld b k f → par' c = .fld b k f)
    (hs : SidPres μ μ') : ∀ b l, Anc par' b l → ∃ blk, μ'[b]? = some blk ∧ BlkOK P μ' par' b blk := by
  intro b l ha
  rcases hco b l ha with ⟨l', ha'⟩ | hn
  · obtain ⟨blk, hb, hok⟩ := hH b l' ha'
    by_cases hm : M b
    · exact hmod b blk l l' hm hb hok ha' ha
    · exact ⟨blk, by rw [hsame b l' hm ha']; exact hb, hok.mono hs (fun c k f hl hc => hkids b l' c k f hm ha' hl hc)⟩
  · exact hnew b l hn ha

theorem Inv.setVar {Γ : Ctx} {ρ μ par x v} (hI : Inv P D d θ Γ ρ μ par)
    (hx : VOK D d θ Γ.roots (ρ.set x v) μ par x) : Inv P D d θ (Γ.add x) (ρ.set x v) μ par := by
  refine ⟨hI.theta, hI.dom, hI.heap, fun y hy => ?_⟩
  rcases mem_add.mp hy with rfl | hy
  · exact hx
  · by_cases e : y = x
    · subst e; exact hx
    · exact (hI.vars y hy).keep (K := fun _ => False) (fun _ _ h _ => h rfl) (by simp [Env.set, e])
        SidPres.refl (fun _ => rfl) (fun _ _ _ _ => rfl) (fun _ _ _ h => h)

theorem Inv.setRef {Γ : Ctx} {ρ μ par x v r} (hI : Inv P D d θ Γ ρ μ par)
    (hx : VOK D d θ (Γ.addRef x r).roots (ρ.set x v) μ par x) :
    Inv P D d θ (Γ.addRef x r) (ρ.set x v) μ par := by
  refine ⟨hI.theta, hI.dom, hI.heap, fun y hy => ?_⟩
  by_cases e : y = x
  · subst e; exact hx
  · have hy : y ∈ Γ.live := by rcases mem_addRef.mp hy with h | h; exact absurd h e; exact h
    exact (hI.vars y hy).keep (K := fun _ => False) (fun _ _ h _ => h rfl) (by simp [Env.set, e])
      SidPres.refl (fun _ => by rw [addRef_lookup]; simp [e]) (fun _ _ _ _ => rfl) (fun _ _ _ h => h)

end

/-! ## Statements without owner changes -/

section
variable {P : Prog} {D : Decls} {d : Nat} {θ : Nat → Lbl} {Γ Γ' : Ctx} {ρ : Env} {μ : Mem} {par : PMap}

theorem Good.inv {ρ'} (h : Inv P D d θ Γ' ρ' μ par) : Good P D d θ μ par Γ' (.ok ρ' μ) :=
  ⟨par, h, Frame.refl, SidPres.refl⟩

theorem Good.trap : Good P D d θ μ par Γ' .trap := trivial

theorem case_set {x e n} (hc : chkS Rules.full P D Γ (.set x e) = some Γ') (hI : Inv P D d θ Γ ρ μ par) :
    Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.set x e)) := by
  simp only [chkS] at hc
  split at hc
  · rename_i h; cases hc
    simp only [exec]
    rcases evalE_ok hI e h.2 with he | ⟨m, he⟩
    · rw [he]; exact Good.trap
    · rw [he]; simp only [Res.thenO]
      exact Good.inv (hI.setVar (by unfold VOK; rw [h.1]; exact ⟨m, by simp [Env.set]⟩))
  · cases hc

theorem case_nul {x n} (hc : chkS Rules.full P D Γ (.nul x) = some Γ') (hI : Inv P D d θ Γ ρ μ par) :
    Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.nul x)) := by
  simp only [chkS] at hc
  simp only [exec]
  split at hc <;> rename_i hD <;> cases hc
  · have hI' : Inv P D d θ (Γ.kill (.loc x)) ρ μ par :=
      hI.weaken fun y hy => ⟨(mem_kill.mp hy).1, fun _ => rfl⟩
    exact Good.inv (hI'.setVar (by unfold VOK; rw [hD]; exact .inl (by simp [Env.set])))
  · exact Good.inv (hI.setVar (by unfold VOK; rw [hD]; exact .inl (by simp [Env.set])))

theorem case_borrow {x y i n} (hc : chkS Rules.full P D Γ (.borrow x y i) = some Γ')
    (hI : Inv P D d θ Γ ρ μ par) : Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.borrow x y i)) := by
  simp only [chkS] at hc
  split at hc <;> try cases hc
  rename_i s s' r hD hs hr
  split at hc <;> rename_i h <;> cases hc
  obtain ⟨rfl, hy, hi⟩ := h
  simp only [exec]
  rcases evalE_ok hI i hi with he | ⟨k, he⟩
  · rw [he]; exact Good.trap
  rw [he]; simp only [Res.thenO]
  rcases live_ptr hI hy hs with h0 | ⟨b, off, r', h1, h2, h3, h4, -⟩
  · rw [h0]; simp only
    exact Good.inv (hI.setRef (by unfold VOK; rw [hD]; exact .inl (by simp [Env.set])))
  · rw [h1]; simp only
    rw [hr] at h3; cases h3
    exact Good.inv (hI.setRef (by
      unfold VOK; rw [hD]
      exact .inr ⟨b, off + k, r, by simp [Env.set], h2, by rw [addRef_lookup]; simp, h4⟩))

theorem case_load {x y i f n} (hc : chkS Rules.full P D Γ (.load x y i f) = some Γ')
    (hI : Inv P D d θ Γ ρ μ par) : Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.load x y i f)) := by
  simp only [chkS] at hc
  cases hs : ptrS D y with
  | none => simp [hs] at hc
  | some s =>
  simp only [hs] at hc
  by_cases h : y ∈ Γ.live ∧ chkE P D Γ i = true
  rotate_left
  · simp [h] at hc
  simp only [show (y ∈ Γ.live ∧ chkE P D Γ i = true) = True from eq_true h, ite_true] at hc
  obtain ⟨hy, hi⟩ := h
  simp only [exec]
  rcases fieldAt_ok hI hy hs hi with e | ⟨b, off, r, blk, k, el, e, -, hr, ha, hb, hsid, hok, hk, -⟩
  · rw [e]; exact Good.trap
  rw [e]; simp only [Res.thenO]
  subst hsid
  split at hc <;> try cases hc
  · rename_i hf hD
    obtain ⟨v, -, hrd, hv⟩ := rd_ok hok hb hk hf
    rw [hrd]; simp only
    obtain ⟨m, rfl⟩ := hv
    exact Good.inv (hI.setVar (by unfold VOK; rw [hD]; exact ⟨m, by simp [Env.set]⟩))
  · rename_i s' s'' hf hD
    rw [hr] at hc; simp only at hc
    split at hc <;> rename_i hss <;> cases hc
    subst hss
    obtain ⟨v, -, hrd, hv⟩ := rd_ok hok hb hk hf
    rw [hrd]; simp only
    rcases hv with rfl | ⟨c, rfl, hpc, hsc⟩
    · exact Good.inv (hI.setRef (by unfold VOK; rw [hD]; exact .inl (by simp [Env.set])))
    · exact Good.inv (hI.setRef (by
        unfold VOK; rw [hD]
        exact .inr ⟨c, 0, r, by simp [Env.set], hsc, by rw [addRef_lookup]; simp, .fld hpc ha⟩))

end

/-! ## Statements that move ownership -/

theorem upd_eq {par : PMap} {b p} : upd par b p b = p := by simp [upd]
theorem upd_ne {par : PMap} {b b' p} (h : b' ≠ b) : upd par b p b' = par b' := by simp [upd, h]

theorem getElem?_app_lt {μ : Mem} {nb : Blk} {b} (h : b < μ.length) : (μ ++ [nb])[b]? = μ[b]? :=
  List.getElem?_append_left h

theorem getElem?_set_ne' {μ : Mem} {b b' : Nat} {blk : Blk} (h : b' ≠ b) : (μ.set b blk)[b']? = μ[b']? := by
  simp [Ne.symm h]

theorem getElem?_set_eq' {μ : Mem} {b : Nat} {blk blk' : Blk} (h : μ[b]? = some blk') :
    (μ.set b blk)[b]? = some blk := by
  simp [(List.getElem?_eq_some_iff.mp h).1]

theorem fresh_ok {P : Prog} {μ : Mem} {par : PMap} {b s m} : BlkOK P μ par b (freshBlk P s m) := by
  refine ⟨rfl, fun k el hk => ?_⟩
  simp only [freshBlk, List.getElem?_replicate] at hk
  split at hk <;> cases hk
  have hsid : (freshBlk P s m).sid = s := rfl
  rw [hsid]
  refine ⟨by simp [zeroElem, fields], fun f t v ht hv => ?_⟩
  simp only [zeroElem, List.getElem?_map] at hv
  rw [show (P.structs[s]?).getD [] = fields P s from rfl, ht] at hv
  cases hv; cases t <;> simp [zeroOf, FOK]

section
variable {P : Prog} {D : Decls} {d : Nat} {θ : Nat → Lbl} {Γ Γ' : Ctx} {ρ : Env} {μ : Mem} {par : PMap}

theorem Inv.anc_lt (hI : Inv P D d θ Γ ρ μ par) {b l} (h : Anc par b l) : b < μ.length := by
  obtain ⟨blk, hb, -⟩ := hI.heap b l h
  exact (List.getElem?_eq_some_iff.mp hb).1

theorem Inv.no_anc_len (hI : Inv P D d θ Γ ρ μ par) {l} : ¬ Anc par μ.length l :=
  fun h => by have := hI.anc_lt h; omega

/-- A fresh block `μ.length` owned by `x`; the rest unchanged. -/
theorem inv_fresh (hI : Inv P D d θ Γ ρ μ par) {x s m} {Γ1 : Ctx}
    (hD : D.lookup x = some (.own s)) (hlive : ∀ y ∈ Γ1.live, y = x ∨ (y ∈ Γ.live ∧ Γ.roots.lookup y ≠ some (.loc x)))
    (hroots : Γ1.roots = Γ.roots) :
    Inv P D d θ Γ1 (ρ.set x (.ptr μ.length 0)) (μ ++ [freshBlk P s m]) (upd par μ.length (.var (d, x))) ∧
    Frame d par (upd par μ.length (.var (d, x))) := by
  have hch : ∀ b, upd par μ.length (.var (d, x)) b ≠ par b → b = μ.length := by
    intro b e; apply Classical.byContradiction; intro h; exact e (upd_ne h)
  have hlen : (μ ++ [freshBlk P s m]).length = μ.length + 1 := by simp
  refine ⟨⟨hI.theta, ?_, ?_, ?_⟩, frame_of fun b l e ha => absurd (hch b e ▸ ha) hI.no_anc_len⟩
  · intro c b k f hc
    have hne : c ≠ μ.length := by intro h; subst h; simp [upd] at hc
    rw [upd_ne hne] at hc; have := hI.dom c b k f hc; omega
  · refine heap_of (N := (· = μ.length)) (M := fun _ => False) hI.heap ?_ ?_ ?_ ?_ ?_ sidPres_app
    · exact fun b l ha => anc_coarse (fun b e _ => .inr (hch b e))
        (fun b b' k f hp hn => by have := hI.dom b b' k f hp; omega) ha
    · intro b l hn _; subst hn; exact ⟨_, getElem?_app_len, fresh_ok⟩
    · intro _ _ _ _ h; exact h.elim
    · intro b l _ ha; exact getElem?_app_lt (hI.anc_lt ha)
    · intro b l c k f _ _ hl hc; rw [upd_ne (by omega)]; exact hc
  · intro y hy
    rcases hlive y hy with rfl | ⟨hy, -⟩
    · unfold VOK; rw [hD]
      exact .inr ⟨μ.length, by simp [Env.set], upd_eq, by simp [sidOf, freshBlk]⟩
    · by_cases e : y = x
      · subst e; unfold VOK; rw [hD]
        exact .inr ⟨μ.length, by simp [Env.set], upd_eq, by simp [sidOf, freshBlk]⟩
      exact (hI.vars y hy).keep (K := fun _ => False)
        (fun b l e ha => absurd (hch b e ▸ ha) hI.no_anc_len) (by simp [Env.set, e]) sidPres_app
        (fun _ => by rw [hroots])
        (fun _ b _ hb => upd_ne (by have := hI.anc_lt (.var hb); omega)) (fun _ _ _ h => h)

theorem case_malloc {x s e n} (hc : chkS Rules.full P D Γ (.malloc x s e) = some Γ')
    (hI : Inv P D d θ Γ ρ μ par) : Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.malloc x s e)) := by
  simp only [chkS] at hc
  split at hc <;> rename_i h <;> cases hc
  obtain ⟨hD, -, he⟩ := h
  simp only [exec]
  rcases evalE_ok hI e he with h | ⟨m, h⟩
  · rw [h]; exact Good.trap
  rw [h]; simp only [Res.thenO]
  split
  · exact Good.trap
  obtain ⟨h1, h2⟩ := inv_fresh (m := m.toNat) hI hD (fun y hy => mem_assignOwn.mp hy) rfl
  exact ⟨_, h1, h2, sidPres_app⟩

end

section
variable {P : Prog} {D : Decls} {d : Nat} {θ : Nat → Lbl} {Γ Γ' : Ctx} {ρ : Env} {μ : Mem} {par : PMap}

/-- Heap and domain after writing field `(k, f)` of block `b`. -/
theorem heap_setc (hI : Inv P D d θ Γ ρ μ par) {b blk k el f v lb} {par' : PMap}
    (hb : μ[b]? = some blk) (hok : BlkOK P μ par b blk) (hk : blk.cells[k]? = some el) (ha : Anc par b lb)
    (hch : ∀ c, par' c ≠ par c → par' c ≠ .nil → ∃ l, Anc par c l)
    (hkid : ∀ c b2 k2 f2, c < μ.length → par c = .fld b2 k2 f2 → (b2 = b → k2 = k → f2 ≠ f) → par' c = par c)
    (hdom : ∀ c b2 k2 f2, par' c = .fld b2 k2 f2 → b2 < μ.length)
    (hv : ∀ t, (fields P blk.sid)[f]? = some t →
      FOK (μ.set b { blk with cells := blk.cells.set k (el.set f v) }) par' b k f t v) :
    (∀ c b2 k2 f2, par' c = .fld b2 k2 f2 → b2 < (μ.set b { blk with cells := blk.cells.set k (el.set f v) }).length) ∧
    (∀ b2 l, Anc par' b2 l → ∃ blk2, (μ.set b { blk with cells := blk.cells.set k (el.set f v) })[b2]? = some blk2 ∧
      BlkOK P (μ.set b { blk with cells := blk.cells.set k (el.set f v) }) par' b2 blk2) := by
  have hs : SidPres μ (μ.set b { blk with cells := blk.cells.set k (el.set f v) }) := sidPres_set hb rfl
  refine ⟨fun c b2 k2 f2 h => by rw [List.length_set]; exact hdom c b2 k2 f2 h, ?_⟩
  refine heap_of (N := fun _ => False) (M := (· = b)) hI.heap
    (fun b l ha => anc_coarse (fun b e hn => .inl (hch b e hn)) (fun _ _ _ _ _ h => h) ha)
    (fun _ _ h => h.elim) ?_ ?_ ?_ hs
  · intro b' blk' l l' hm hb' _ _ _
    subst hm; rw [hb] at hb'; cases hb'
    refine ⟨_, getElem?_set_eq' hb, hok.setc hk hs ?_ hv⟩
    intro c k' f' hne hl hc
    rw [hkid c _ k' f' hl hc (fun _ h1 => hne h1)]
    exact hc
  · intro b' l hm _; exact getElem?_set_ne' hm
  · intro b' l c k' f' hm _ hl hc
    rw [hkid c b' k' f' hl hc (fun h => absurd h hm)]; exact hc

end

section
variable {P : Prog} {D : Decls} {d : Nat} {θ : Nat → Lbl} {Γ Γ' : Ctx} {ρ : Env} {μ : Mem} {par : PMap}

theorem rootOf_own {Γ : Ctx} {y s} (h : D.lookup y = some (.own s)) : rootOf D Γ y = some (.loc y) := by
  simp [rootOf, h]

theorem ownerOK_full {y s} (h : ownerOK Rules.full D y s = true) : D.lookup y = some (.own s) := by
  simpa [ownerOK, Rules.full] using h

theorem killRoot_own {Γ : Ctx} {y s} (h : D.lookup y = some (.own s)) :
    killRoot true D Γ y = Γ.kill (.loc y) := by
  simp [killRoot, rootOf_own h]

theorem own_head (hI : Inv P D d θ Γ ρ μ par) {x s} (hx : x ∈ Γ.live) (hD : D.lookup x = some (.own s)) :
    ρ x = .null ∨ ∃ b, ρ x = .ptr b 0 ∧ par b = .var (d, x) ∧ sidOf μ b = some s := by
  have h := hI.vars x hx; unfold VOK at h; rw [hD] at h; exact h

theorem anc_KS1 {b l x} (hb : par b = .var (d, x)) (ha : Anc par b l) : KS d [x] l := by
  rw [Anc.det ha (.var hb)]; simp [KS]

theorem case_free {x n} (hc : chkS Rules.full P D Γ (.free x) = some Γ')
    (hI : Inv P D d θ Γ ρ μ par) : Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.free x)) := by
  simp only [chkS] at hc
  cases hps : ptrS D x with
  | none => simp [hps] at hc
  | some s =>
  simp only [hps] at hc
  split at hc <;> rename_i h <;> cases hc
  obtain ⟨ho, hx⟩ := h
  have hD := ownerOK_full ho
  simp only [Rules.full]
  rw [killRoot_own hD]
  have hw : ∀ y ∈ ((Γ.kill (.loc x)).drop x).live, y ∈ Γ.live ∧ Γ.roots.lookup y ≠ some (.loc x) ∧ y ≠ x :=
    fun y hy => by have := mem_drop.mp hy; have := mem_kill.mp this.1; exact ⟨this.1, this.2, ‹_ ∧ _›.2⟩
  simp only [exec]
  rcases own_head hI hx hD with h0 | ⟨b, h1, h2, h3⟩
  · rw [h0]; exact Good.inv (hI.weaken fun y hy => ⟨(hw y hy).1, fun _ => rfl⟩)
  obtain ⟨blk, hb, hok⟩ := hI.heap b _ (.var h2)
  rw [h1]; simp only [hb, hok.1]
  simp only [Bool.true_eq_false, ite_false, ite_true]
  have hch : ∀ c, upd par b .nil c ≠ par c → c = b := fun c e =>
    Classical.byContradiction fun h => e (upd_ne h)
  have hs : SidPres μ (kill μ b blk) := sidPres_set hb rfl
  refine ⟨upd par b .nil, ⟨hI.theta, ?_, ?_, ?_⟩, frame_KS (ks := [x]) fun c l e ha => ?_, hs⟩
  · intro c b2 k f hc
    have : c ≠ b := by intro h; subst h; simp [upd] at hc
    rw [upd_ne this] at hc; rw [kill, List.length_set]; exact hI.dom c b2 k f hc
  · refine heap_of (N := fun _ => False) (M := (· = b)) hI.heap
      (fun b' l ha => anc_coarse (fun c e hn => absurd (by rw [hch c e]; exact upd_eq) hn)
        (fun _ _ _ _ _ h => h) ha)
      (fun _ _ h => h.elim) ?_ ?_ ?_ hs
    · intro b' _ l _ hm _ _ _ ha; subst hm; exact absurd upd_eq ha.ne_nil
    · intro b' l hm _; exact getElem?_set_ne' hm
    · intro b' l c k f _ _ _ hc
      rw [upd_ne (by intro h; subst h; rw [h2] at hc; cases hc)]; exact hc
  · intro y hy
    obtain ⟨hy, hr, hne⟩ := hw y hy
    refine (hI.vars y hy).keep (K := KS d [x]) (fun c l e ha => by rw [hch c e] at ha; exact anc_KS1 h2 ha)
      rfl hs (fun _ => rfl) (fun _ b2 _ hb2 => upd_ne (by
        intro h; subst h; rw [h2] at hb2; cases hb2; exact hne rfl)) ?_
    intro _ rt hrt
    exact lab_notKS hI.theta (by intro k hk; simp at hk; subst hk; intro h; subst h; exact hr hrt)
  · rw [hch c e] at ha; exact anc_KS1 h2 ha

end

section
variable {P : Prog} {D : Decls} {d : Nat} {θ : Nat → Lbl} {Γ Γ' : Ctx} {ρ : Env} {μ : Mem} {par : PMap}

theorem vars_upd (hI : Inv P D d θ Γ ρ μ par) {ks : List Nat} {Γ1 : Ctx} {x} {ρ' : Env} {μ' : Mem}
    {par' : PMap}
    (hch : ∀ c l, par' c ≠ par c → Anc par c l → KS d ks l) (hs : SidPres μ μ')
    (he : ∀ z, z ≠ x → ρ' z = ρ z)
    (hx : x ∈ Γ1.live → VOK D d θ Γ1.roots ρ' μ' par' x)
    (hlive : ∀ z ∈ Γ1.live, z ≠ x → z ∈ Γ.live ∧ ∀ k ∈ ks, Γ.roots.lookup z ≠ some (.loc k))
    (hroots : ∀ z, z ≠ x → Γ1.roots.lookup z = Γ.roots.lookup z)
    (hown : ∀ z b, z ∈ Γ1.live → z ≠ x → par b = .var (d, z) → par' b = par b) :
    ∀ z ∈ Γ1.live, VOK D d θ Γ1.roots ρ' μ' par' z := by
  intro z hz
  by_cases e : z = x
  · subst e; exact hx hz
  obtain ⟨hz', hr⟩ := hlive z hz e
  exact (hI.vars z hz').keep hch (he z e) hs (fun _ => hroots z e) (fun _ b _ hb => hown z b hz e hb)
    (fun _ rt hrt => lab_notKS hI.theta fun k hk h => hr k hk (h ▸ hrt))

theorem case_move {x y n} (hc : chkS Rules.full P D Γ (.move x y) = some Γ')
    (hI : Inv P D d θ Γ ρ μ par) : Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.move x y)) := by
  simp only [chkS] at hc
  split at hc <;> try cases hc
  rename_i s s' hDx hDy
  split at hc <;> rename_i h <;> cases hc
  obtain ⟨rfl, hxy, hy⟩ := h
  simp only [consume, Rules.full, ite_true]
  have hl : ∀ z ∈ ((Γ.kill (.loc y)).drop y |>.assignOwn x).live, z ≠ x →
      z ∈ Γ.live ∧ ∀ k ∈ [y], Γ.roots.lookup z ≠ some (.loc k) := by
    intro z hz e
    rcases mem_assignOwn.mp hz with h | ⟨h, -⟩
    · exact absurd h e
    · have := mem_drop.mp h; have := mem_kill.mp this.1; simpa using this
  have hl2 : ∀ z ∈ ((Γ.kill (.loc y)).drop y |>.assignOwn x).live, z ≠ y := by
    intro z hz e; subst e
    rcases mem_assignOwn.mp hz with h | ⟨h, -⟩
    · exact hxy h.symm
    · exact (mem_drop.mp h).2 rfl
  simp only [exec]
  rcases own_head hI hy hDy with h0 | ⟨b, h1, h2, h3⟩
  · rw [h0]; simp only [reduceCtorEq, ite_false]
    refine Good.inv ⟨hI.theta, hI.dom, hI.heap, vars_upd hI (ks := [y]) (fun _ _ e _ => absurd rfl e)
      SidPres.refl (fun z e => by simp [Env.set, e]) (fun _ => ?_) hl (fun _ _ => rfl) (fun _ _ _ _ _ => rfl)⟩
    unfold VOK; rw [hDx]; exact .inl (by simp [Env.set])
  rw [h1]; simp only [reduceCtorEq, ite_false]
  have hch : ∀ c, upd par b (.var (d, x)) c ≠ par c → c = b := fun c e =>
    Classical.byContradiction fun h => e (upd_ne h)
  have hK : ∀ c l, upd par b (.var (d, x)) c ≠ par c → Anc par c l → KS d [y] l :=
    fun c l e ha => by rw [hch c e] at ha; exact anc_KS1 h2 ha
  refine ⟨upd par b (.var (d, x)), ⟨hI.theta, ?_, ?_, ?_⟩, frame_KS hK, SidPres.refl⟩
  · intro c b2 k f hc
    rw [upd_ne (by intro h; subst h; simp [upd] at hc)] at hc; exact hI.dom c b2 k f hc
  · refine heap_of (N := fun _ => False) (M := fun _ => False) hI.heap
      (fun b' l ha => anc_coarse (fun c e _ => .inl ⟨_, by obtain rfl := hch c e; exact Anc.var h2⟩) (fun _ _ _ _ _ h => h) ha)
      (fun _ _ h => h.elim) (fun _ _ _ _ h => h.elim) (fun _ _ _ _ => rfl) ?_ SidPres.refl
    intro b' l c k f _ _ _ hc
    rw [upd_ne (by intro h; subst h; rw [h2] at hc; cases hc)]; exact hc
  · refine vars_upd hI hK SidPres.refl (fun z e => by simp [Env.set, e]) (fun _ => ?_) hl
      (fun _ _ => rfl) (fun z b2 hz _ hb2 => upd_ne (by
        intro h; subst h; rw [h2] at hb2; have : y = z := by simpa using hb2
        exact hl2 z hz this.symm))
    unfold VOK; rw [hDx]; exact .inr ⟨b, by simp [Env.set], upd_eq, h3⟩


theorem wrF_eq {b k f v} {blk : Blk} {el : List V} (hb : μ[b]? = some blk) (hk : blk.cells[k]? = some el)
    (hf : f < el.length) :
    wrF μ b k f v = .ok (μ.set b { blk with cells := blk.cells.set k (el.set f v) }) := by
  unfold wrF; rw [getc_eq hb hk, List.getElem?_eq_getElem hf]; simp only; rw [setc_eq hb hk]

theorem field_lt {b : Nat} {blk : Blk} {k f : Nat} {t : FTy} {el : List V} (hok : BlkOK P μ par b blk) (hk : blk.cells[k]? = some el)
    (ht : fieldTy P blk.sid f = some t) : f < el.length := by
  rw [fieldTy_eq] at ht; rw [(hok.2 k el hk).1]; exact (List.getElem?_eq_some_iff.mp ht).1

theorem case_take {x y i f n} (hc : chkS Rules.full P D Γ (.take x y i f) = some Γ')
    (hI : Inv P D d θ Γ ρ μ par) : Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.take x y i f)) := by
  simp only [chkS] at hc
  split at hc <;> try cases hc
  rename_i s s' hps hDx
  split at hc <;> rename_i h <;> cases hc
  obtain ⟨ho, hy, hxy, hi, hf⟩ := h
  have hDy := ownerOK_full ho
  simp only [Rules.full]; rw [killRoot_own hDy]
  have hl : ∀ z ∈ ((Γ.kill (.loc y)).assignOwn x).live, z ≠ x →
      z ∈ Γ.live ∧ ∀ k ∈ [y], Γ.roots.lookup z ≠ some (.loc k) := by
    intro z hz e
    rcases mem_assignOwn.mp hz with h | ⟨h, -⟩
    · exact absurd h e
    · have := mem_kill.mp h; simpa using this
  simp only [exec]
  rcases fieldAt_ok hI hy hps hi with e | ⟨b, off, r, blk, k, el, e, -, -, -, hb, hsid, hok, hk, hown⟩
  · rw [e]; exact Good.trap
  obtain ⟨-, hpb⟩ := hown (by simp [isOwn, hDy])
  subst hsid
  obtain ⟨v, -, hrd, hv⟩ := rd_ok hok hb hk hf
  have hfl := field_lt hok hk hf
  rw [e]; simp only [Res.thenO]; rw [hrd]; simp only; rw [wrF_eq hb hk hfl]; simp only
  have hs := sidPres_set (blk' := { blk with cells := blk.cells.set k (el.set f .null) }) hb rfl
  have tail : ∀ par' : PMap, (∀ c, par' c ≠ par c → par c = .fld b k f) →
      (∀ c b2 k2 f2, par' c = .fld b2 k2 f2 → par c = .fld b2 k2 f2) →
      VOK D d θ ((Γ.kill (.loc y)).assignOwn x).roots (ρ.set x v)
        (μ.set b { blk with cells := blk.cells.set k (el.set f .null) }) par' x →
      Good P D d θ μ par ((Γ.kill (.loc y)).assignOwn x)
        (.ok (ρ.set x v) (μ.set b { blk with cells := blk.cells.set k (el.set f .null) })) := by
    intro par' hchc hfld hx
    have hK : ∀ c l, par' c ≠ par c → Anc par c l → KS d [y] l := fun c l e ha => by
      rw [Anc.det ha (.fld (hchc c e) (.var hpb))]; simp [KS]
    obtain ⟨hdom, hheap⟩ := heap_setc hI hb hok hk (.var hpb)
      (fun c e _ => ⟨_, .fld (hchc c e) (.var hpb)⟩)
      (fun c b2 k2 f2 _ hc hne => Classical.byContradiction fun e => by
        rw [hchc c e] at hc; cases hc; exact hne rfl rfl rfl)
      (fun c b2 k2 f2 h => hI.dom c b2 k2 f2 (hfld c b2 k2 f2 h))
      (fun t ht => by rw [← fieldTy_eq] at ht; rw [hf] at ht; cases ht; exact .inl rfl)
    refine ⟨par', ⟨hI.theta, hdom, hheap, vars_upd hI hK hs (fun z e => by simp [Env.set, e])
      (fun _ => hx) hl (fun _ _ => rfl) ?_⟩, frame_KS hK, hs⟩
    intro z b2 _ _ hb2
    exact Classical.byContradiction fun e => by rw [hchc b2 e] at hb2; cases hb2
  rcases hv with rfl | ⟨c, rfl, hpc, hsc⟩
  · exact tail par (fun _ e => absurd rfl e) (fun _ _ _ _ h => h)
      (by unfold VOK; rw [hDx]; exact .inl (by simp [Env.set]))
  · refine tail (upd par c (.var (d, x)))
      (fun c' e => by
        have : c' = c := Classical.byContradiction fun h => e (upd_ne h)
        subst this; exact hpc)
      (fun c' b2 k2 f2 h => by
        by_cases e : c' = c
        · subst e; simp [upd] at h
        · rw [upd_ne e] at h; exact h) ?_
    unfold VOK; rw [hDx]; exact .inr ⟨c, by simp [Env.set], upd_eq, hs _ _ hsc⟩

theorem case_put {y i f z n} (hc : chkS Rules.full P D Γ (.put y i f z) = some Γ')
    (hI : Inv P D d θ Γ ρ μ par) : Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.put y i f z)) := by
  simp only [chkS] at hc
  split at hc <;> try cases hc
  rename_i s s' hps hDz
  split at hc <;> rename_i h <;> cases hc
  obtain ⟨ho, hy, hz, hzy, hi, hf⟩ := h
  have hDy := ownerOK_full ho
  simp only [Rules.full, consume, ite_true]; rw [killRoot_own hDy]
  have hl : ∀ w ∈ (((Γ.kill (.loc y)).kill (.loc z)).drop z).live, w ≠ z →
      w ∈ Γ.live ∧ ∀ k ∈ [z], Γ.roots.lookup w ≠ some (.loc k) := by
    intro w hw _
    have h1 := mem_drop.mp hw; have h2 := mem_kill.mp h1.1; have h3 := mem_kill.mp h2.1
    exact ⟨h3.1, by simpa using h2.2⟩
  simp only [exec]
  rcases fieldAt_ok hI hy hps hi with e | ⟨b, off, r, blk, k, el, e, -, -, -, hb, hsid, hok, hk, hown⟩
  · rw [e]; exact Good.trap
  obtain ⟨-, hpb⟩ := hown (by simp [isOwn, hDy])
  subst hsid
  have hfl := field_lt hok hk hf
  rw [e]; simp only [Res.thenO]
  rcases own_head hI hz hDz with h0 | ⟨hz', h1, h2, h3⟩
  · rw [h0]; simp only [reduceCtorEq, ite_false]; rw [wrF_eq hb hk hfl]; simp only
    have hs := sidPres_set (blk' := { blk with cells := blk.cells.set k (el.set f .null) }) hb rfl
    obtain ⟨hdom, hheap⟩ := heap_setc (par' := par) hI hb hok hk (.var hpb) (fun _ e => absurd rfl e)
      (fun _ _ _ _ _ _ _ => rfl) hI.dom
      (fun t ht => by rw [← fieldTy_eq] at ht; rw [hf] at ht; cases ht; exact .inl rfl)
    refine ⟨par, ⟨hI.theta, hdom, hheap, vars_upd hI (ks := [z]) (fun _ _ e _ => absurd rfl e) hs
      (fun _ _ => rfl) (fun h => absurd rfl (mem_drop.mp h).2) hl (fun _ _ => rfl)
      (fun _ _ _ _ _ => rfl)⟩, Frame.refl, hs⟩
  rw [h1]; simp only [reduceCtorEq, ite_false]; rw [wrF_eq hb hk hfl]; simp only
  have hs := sidPres_set (blk' := { blk with cells := blk.cells.set k (el.set f (.ptr hz' 0)) }) hb rfl
  have hch : ∀ c, upd par hz' (.fld b k f) c ≠ par c → c = hz' := fun c e =>
    Classical.byContradiction fun h => e (upd_ne h)
  have hK : ∀ c l, upd par hz' (.fld b k f) c ≠ par c → Anc par c l → KS d [z] l :=
    fun c l e ha => by rw [hch c e] at ha; exact anc_KS1 h2 ha
  obtain ⟨hdom, hheap⟩ := heap_setc hI hb hok hk (.var hpb)
    (fun c e _ => ⟨_, by obtain rfl := hch c e; exact .var h2⟩)
    (fun c b2 k2 f2 _ hc _ => upd_ne (by intro h; subst h; rw [h2] at hc; cases hc))
    (fun c b2 k2 f2 h => by
      by_cases e : c = hz'
      · subst e; rw [upd_eq] at h; cases h; exact (List.getElem?_eq_some_iff.mp hb).1
      · rw [upd_ne e] at h; exact hI.dom c b2 k2 f2 h)
    (fun t ht => by
      rw [← fieldTy_eq] at ht; rw [hf] at ht; cases ht
      exact .inr ⟨hz', rfl, upd_eq, hs _ _ h3⟩)
  refine ⟨_, ⟨hI.theta, hdom, hheap, vars_upd hI hK hs (fun _ _ => rfl)
    (fun h => absurd rfl (mem_drop.mp h).2) hl (fun _ _ => rfl) ?_⟩, frame_KS hK, hs⟩
  intro w b2 _ hw hb2
  exact upd_ne (by intro h; subst h; rw [h2] at hb2; have : z = w := by simpa using hb2
                   exact hw this.symm)


theorem case_store {y i f e n} (hc : chkS Rules.full P D Γ (.store y i f e) = some Γ')
    (hI : Inv P D d θ Γ ρ μ par) : Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.store y i f e)) := by
  simp only [chkS] at hc
  cases hps : ptrS D y with
  | none => simp [hps] at hc
  | some s =>
  simp only [hps] at hc
  split at hc <;> rename_i h <;> cases hc
  obtain ⟨hy, hi, he, hf⟩ := h
  simp only [exec]
  rcases fieldAt_ok hI hy hps hi with e | ⟨b, off, r, blk, k, el, e, -, -, ha, hb, hsid, hok, hk, -⟩
  · rw [e]; exact Good.trap
  subst hsid
  have hfl := field_lt hok hk hf
  rw [e]; simp only [Res.thenO]
  rcases evalE_ok hI _ he with h | ⟨m, h⟩
  · rw [h]; exact Good.trap
  rw [h]; simp only; rw [wrF_eq hb hk hfl]; simp only
  have hs := sidPres_set (blk' := { blk with cells := blk.cells.set k (el.set f (.int m)) }) hb rfl
  obtain ⟨hdom, hheap⟩ := heap_setc (par' := par) hI hb hok hk ha (fun _ e => absurd rfl e)
    (fun _ _ _ _ _ _ _ => rfl) hI.dom
    (fun t ht => by rw [← fieldTy_eq] at ht; rw [hf] at ht; cases ht; exact ⟨m, rfl⟩)
  exact ⟨par, ⟨hI.theta, hdom, hheap, fun z hz => (hI.vars z hz).keep (K := fun _ => False)
    (fun _ _ e _ => absurd rfl e) rfl hs (fun _ => rfl) (fun _ _ _ _ => rfl) (fun _ _ _ h => h)⟩,
    Frame.refl, hs⟩

theorem zero_el {μ : Mem} {par : PMap} {b k s} :
    (zeroElem P s).length = (fields P s).length ∧
    ∀ f t v, (fields P s)[f]? = some t → (zeroElem P s)[f]? = some v → FOK μ par b k f t v := by
  refine ⟨by simp [zeroElem, fields], fun f t v ht hv => ?_⟩
  simp only [zeroElem, List.getElem?_map] at hv
  rw [show (P.structs[s]?).getD [] = fields P s from rfl, ht] at hv
  cases hv; cases t <;> simp [zeroOf, FOK]

theorem realloc_blk {μ μ' : Mem} {par par' : PMap} {b blk s m nb} (hok : BlkOK P μ par b blk)
    (hsid : blk.sid = s) (hs : SidPres μ μ')
    (hkid : ∀ c k f, c < μ.length → par c = .fld b k f → par' c = .fld nb k f) :
    BlkOK P μ' par' nb ⟨true, s, blk.cells.take m ++ List.replicate (m - blk.cells.length) (zeroElem P s)⟩ := by
  refine ⟨rfl, fun k el hk => ?_⟩
  simp only
  by_cases hlt : k < (blk.cells.take m).length
  · rw [List.getElem?_append_left hlt, List.getElem?_take] at hk
    split at hk
    rotate_left
    · cases hk
    obtain ⟨hl, hf⟩ := hok.2 k el hk
    rw [hsid] at hl hf
    refine ⟨hl, fun f t v ht hv => ?_⟩
    have h := hf f t v ht hv
    cases t with
    | int => exact h
    | own s' =>
      rcases h with h | ⟨c, h1, h2, h3⟩
      · exact .inl h
      · exact .inr ⟨c, h1, hkid c k f (sidOf_lt h3) h2, hs c s' h3⟩
  · rw [List.getElem?_append_right (by omega), List.getElem?_replicate] at hk
    split at hk <;> cases hk
    exact zero_el

def moveKids (par : PMap) (b nb : Nat) (p : Par) : PMap := fun c =>
  if c = b then .nil else if c = nb then p else
  match par c with
  | .fld b' k f => if b' = b then .fld nb k f else .fld b' k f
  | q => q

theorem moveKids_ch {par : PMap} {b nb p c} (h : moveKids par b nb p c ≠ par c) :
    c = b ∨ c = nb ∨ ∃ k f, par c = .fld b k f := by
  unfold moveKids at h
  by_cases e1 : c = b; · exact .inl e1
  by_cases e2 : c = nb; · exact .inr (.inl e2)
  simp only [e1, e2, ite_false] at h
  split at h
  · rename_i b' k f hp
    by_cases e3 : b' = b
    · subst e3; exact .inr (.inr ⟨k, f, hp⟩)
    · simp [e3, hp] at h
  · rename_i q hq; exact absurd rfl h

theorem moveKids_un {par : PMap} {b nb p c} (h1 : c ≠ b) (h2 : c ≠ nb) (h3 : ∀ k f, par c ≠ .fld b k f) :
    moveKids par b nb p c = par c := by
  apply Classical.byContradiction; intro e
  rcases moveKids_ch e with h | h | ⟨k, f, h⟩
  · exact h1 h
  · exact h2 h
  · exact h3 k f h

theorem moveKids_fld {par : PMap} {b nb : Nat} {p : Par} {c b2 k f : Nat} (hp : ∀ b k f, p ≠ .fld b k f)
    (h : moveKids par b nb p c = .fld b2 k f) : b2 = nb ∨ par c = .fld b2 k f := by
  unfold moveKids at h
  split at h
  · cases h
  split at h
  · exact absurd h (hp _ _ _)
  split at h
  · rename_i b' k' f' hq
    split at h
    · cases h; exact .inl rfl
    · cases h; exact .inr hq
  · simp_all

theorem case_realloc {x s e n} (hc : chkS Rules.full P D Γ (.realloc x s e) = some Γ')
    (hI : Inv P D d θ Γ ρ μ par) : Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.realloc x s e)) := by
  simp only [chkS] at hc
  split at hc <;> rename_i h <;> cases hc
  obtain ⟨hD, hx, he⟩ := h
  simp only [Rules.full, ite_true]
  have hl : ∀ z ∈ (Γ.kill (.loc x)).live, z ≠ x → z ∈ Γ.live ∧ ∀ k ∈ [x], Γ.roots.lookup z ≠ some (.loc k) :=
    fun z hz _ => by have := mem_kill.mp hz; simpa using this
  simp only [exec]
  rcases evalE_ok hI e he with h | ⟨m, h⟩
  · rw [h]; exact Good.trap
  rw [h]; simp only [Res.thenO]
  split
  · exact Good.trap
  rcases own_head hI hx hD with h0 | ⟨b, h1, h2, h3⟩
  · rw [h0]; simp only
    obtain ⟨h1, h2⟩ := inv_fresh (m := m.toNat) (Γ1 := Γ.kill (.loc x)) hI hD
      (fun y hy => by have := mem_kill.mp hy; by_cases e : y = x; exact .inl e; exact .inr this) rfl
    exact ⟨_, h1, h2, sidPres_app⟩
  obtain ⟨blk, hb, hok⟩ := hI.heap b _ (.var h2)
  obtain ⟨blk', hb', hsid⟩ := sidOf_some h3
  rw [hb] at hb'; cases hb'
  rw [h1]; simp only [hb, hok.1, Bool.true_eq_false, ite_false, ite_true]
  have hlen : (kill μ b blk).length = μ.length := List.length_set
  have hs : SidPres μ (kill μ b blk ++ [⟨true, s, blk.cells.take m.toNat ++
      List.replicate (m.toNat - blk.cells.length) (zeroElem P s)⟩]) :=
    by unfold kill; exact (sidPres_set (blk' := { blk with live := false }) hb rfl).trans sidPres_app
  have hbl : b < μ.length := (List.getElem?_eq_some_iff.mp hb).1
  have hK : ∀ c l, moveKids par b μ.length (.var (d, x)) c ≠ par c → Anc par c l → KS d [x] l := by
    intro c l e ha
    rcases moveKids_ch e with rfl | rfl | ⟨k, f, hp⟩
    · exact anc_KS1 h2 ha
    · exact absurd ha hI.no_anc_len
    · rw [Anc.det ha (.fld hp (.var h2))]; simp [KS]
  refine ⟨moveKids par b μ.length (.var (d, x)), ⟨hI.theta, ?_, ?_, ?_⟩, frame_KS hK, hs⟩
  · intro c b2 k f hc
    rw [List.length_append, hlen]; simp only [List.length_singleton]
    rcases moveKids_fld (fun _ _ _ h => by cases h) hc with h | h
    · omega
    · have := hI.dom c b2 k f h; omega
  · refine heap_of (N := (· = μ.length)) (M := (· = b)) hI.heap ?_ ?_ ?_ ?_ ?_ hs
    · refine fun b' l ha => anc_coarse (N := (· = μ.length)) (fun c e hn => ?_) (fun c b' k f hp hn => by
        have := hI.dom c b' k f hp; omega) ha
      rcases moveKids_ch e with rfl | rfl | ⟨k, f, hp⟩
      · exact absurd (by simp [moveKids]) hn
      · exact .inr rfl
      · exact .inl ⟨_, .fld hp (.var h2)⟩
    · intro b' l hn _; subst hn
      refine ⟨_, by rw [← hlen]; exact getElem?_app_len, realloc_blk hok hsid hs ?_⟩
      intro c k f hcl hc
      have e1 : c ≠ b := by intro h; subst h; rw [h2] at hc; cases hc
      have e2 : c ≠ μ.length := by omega
      simp [moveKids, e1, e2, hc]
    · intro b' _ l _ hm _ _ _ ha; subst hm; exact absurd (by simp [moveKids]) ha.ne_nil
    · intro b' l hm ha
      rw [getElem?_app_lt (by rw [hlen]; exact hI.anc_lt ha)]; exact getElem?_set_ne' hm
    · intro b' l c k f hm _ hcl hc
      rw [moveKids_un (by intro h; subst h; rw [h2] at hc; cases hc) (by omega)
        (fun k' f' h => by rw [hc] at h; cases h; exact hm rfl)]
      exact hc
  · refine vars_upd hI hK hs (fun z e => by simp [Env.set, e]) (fun _ => ?_) hl (fun _ _ => rfl) ?_
    · unfold VOK; rw [hD]
      refine .inr ⟨μ.length, by simp [Env.set], by simp [moveKids, Nat.ne_of_gt hbl], ?_⟩
      simp [sidOf, ← hlen]
    · intro z b2 hz e hb2
      have := hI.anc_lt (.var hb2)
      exact moveKids_un (by intro h; subst h; rw [h2] at hb2; have : x = z := by simpa using hb2
                            exact e this.symm) (by omega) (fun k f h => by rw [hb2] at h; cases h)

/-! ## Calls: list plumbing -/

theorem lookup_cons {β} {a b : Nat} {v : β} {l : List (Nat × β)} :
    ((a, v) :: l).lookup b = if b = a then some v else l.lookup b := by
  by_cases e : b = a
  · subst e; simp [List.lookup]
  · have : (b == a) = false := by simpa using e
    simp [List.lookup, this, e]

theorem lookup_zip_mem {ps as : List Nat} {p a} (h : (ps.zip as).lookup p = some a) : (p, a) ∈ ps.zip as := by
  induction ps generalizing as with
  | nil => simp at h
  | cons q ps ih =>
    cases as with
    | nil => simp at h
    | cons b as =>
      simp only [List.zip_cons_cons, lookup_cons] at h
      split at h
      · cases h; subst ‹p = q›; simp
      · exact List.mem_cons_of_mem _ (ih h)

theorem lookup_zip_some {ps as : List Nat} {p} (hl : as.length = ps.length) (hp : p ∈ ps) :
    ∃ a, (ps.zip as).lookup p = some a := by
  induction ps generalizing as with
  | nil => simp at hp
  | cons q ps ih =>
    cases as with
    | nil => simp at hl
    | cons b as =>
      simp only [List.zip_cons_cons, lookup_cons]
      by_cases e : p = q
      · exact ⟨b, by simp [e]⟩
      · simp only [e, ite_false]
        exact ih (by simpa using hl) (by simpa [e] using hp)

theorem mem_zip_of_mem {ps as : List Nat} {a} (hl : as.length = ps.length) (ha : a ∈ as) :
    ∃ p, (p, a) ∈ ps.zip as := by
  induction ps generalizing as with
  | nil => cases as <;> simp_all
  | cons q ps ih =>
    cases as with
    | nil => simp at ha
    | cons b as =>
      simp only [List.zip_cons_cons, List.mem_cons] at ha ⊢
      rcases ha with rfl | ha
      · exact ⟨q, .inl rfl⟩
      · obtain ⟨p, hp⟩ := ih (by simpa using hl) ha; exact ⟨p, .inr hp⟩

theorem lookup_zip_inv {ps as : List Nat} {p a} (hn : ps.Nodup) (h : (ps.zip as).lookup p = some a)
    (hc : as.count a = 1) : (as.zip ps).lookup a = some p := by
  induction ps generalizing as with
  | nil => simp at h
  | cons q ps ih =>
    cases as with
    | nil => simp at h
    | cons b as =>
      simp only [List.zip_cons_cons, lookup_cons] at h ⊢
      have hq : q ∉ ps := (List.nodup_cons.mp hn).1
      split at h
      · cases h; subst ‹p = q›; simp
      · rename_i e
        have hab : a ≠ b := by
          intro hab; subst hab
          have hm := lookup_zip_mem h
          have := List.of_mem_zip hm
          rw [List.count_cons_self] at hc
          have : as.count a = 0 := by omega
          exact (List.count_eq_zero.mp this) (List.of_mem_zip hm).2
        rw [List.count_cons_of_ne (Ne.symm hab)] at hc
        simp only [hab, ite_false]
        exact ih (List.nodup_cons.mp hn).2 h hc

theorem lookup_init {decls : Decls} {q r} :
    ∀ l : List Nat, (l.filterMap fun p => if isRef decls p = true then some (p, Root.ext p) else none).lookup q = some r →
      r = .ext q := by
  intro l h
  induction l with
  | nil => simp at h
  | cons p l ih =>
    by_cases hr : isRef decls p = true
    · simp only [List.filterMap_cons, hr, ite_true, lookup_cons] at h
      split at h
      · cases h; subst ‹q = p›; rfl
      · exact ih h
    · simp only [List.filterMap_cons, hr] at h; exact ih (by simpa using h)

theorem lookup_init' {decls : Decls} {q} :
    ∀ l : List Nat, q ∈ l → isRef decls q = true →
      (l.filterMap fun p => if isRef decls p = true then some (p, Root.ext p) else none).lookup q = some (.ext q) := by
  intro l hq hr
  induction l with
  | nil => simp at hq
  | cons p l ih =>
    simp only [List.filterMap_cons]
    by_cases e : q = p
    · subst e; simp [hr]
    · have hq' : q ∈ l := by simpa [e] using hq
      by_cases hr' : isRef decls p = true
      · simp only [hr', ite_true, lookup_cons, e, ite_false]; exact ih hq'
      · simpa [hr'] using ih hq'

/-- What `consumeArgs` guarantees. -/
theorem consumeArgs_spec {D : Decls} :
    ∀ (args : List Nat) (Γ Γm : Ctx), consumeArgs Rules.full D Γ args = some Γm →
      Γm.roots = Γ.roots ∧ (∀ z ∈ Γm.live, z ∈ Γ.live) ∧
      ∀ a ∈ args, isOwn D a = true → a ∈ Γ.live ∧ a ∉ Γm.live ∧ args.count a = 1 ∧
        ∀ z ∈ Γm.live, Γ.roots.lookup z ≠ some (.loc a) := by
  intro args
  induction args with
  | nil => intro Γ Γm h; simp [consumeArgs] at h; subst h; simp
  | cons a as ih =>
    intro Γ Γm h
    simp only [consumeArgs] at h
    split at h
    · rename_i ho
      split at h
      · rename_i hl
        simp only [Rules.full, ite_true] at h
        obtain ⟨h1, h2, h3⟩ := ih _ _ h
        have hsub : ∀ z ∈ Γm.live, z ∈ Γ.live ∧ z ≠ a ∧ Γ.roots.lookup z ≠ some (.loc a) := by
          intro z hz
          have := mem_drop.mp (h2 z hz); have h' := mem_kill.mp this.1
          exact ⟨h'.1, this.2, h'.2⟩
        refine ⟨by rw [h1]; rfl, fun z hz => (hsub z hz).1, fun b hb hob => ?_⟩
        by_cases e : b = a
        · subst e
          have hnot : b ∉ as := fun hm => (mem_drop.mp (h3 b hm hob).1).2 rfl
          refine ⟨hl, fun hm => (hsub b hm).2.1 rfl, ?_, fun z hz => (hsub z hz).2.2⟩
          rw [List.count_cons_self, List.count_eq_zero.mpr hnot]
        · have hb' : b ∈ as := by simpa [e] using hb
          obtain ⟨g1, g2, g3, g4⟩ := h3 b hb' hob
          have g1' := mem_drop.mp g1; have g1'' := mem_kill.mp g1'.1
          refine ⟨g1''.1, g2, by rw [List.count_cons_of_ne (Ne.symm e)]; exact g3, fun z hz => ?_⟩
          have := g4 z hz; simpa using this
      · cases h
    · rename_i ho
      obtain ⟨h1, h2, h3⟩ := ih _ _ h
      refine ⟨h1, h2, fun b hb hob => ?_⟩
      have e : b ≠ a := by intro e; subst e; exact ho hob
      have hb' : b ∈ as := by simpa [e] using hb
      obtain ⟨g1, g2, g3, g4⟩ := h3 b hb' hob
      exact ⟨g1, g2, by rw [List.count_cons_of_ne (Ne.symm e)]; exact g3, g4⟩

/-! ## Calls: the callee's view and the caller's view after the return -/

/-- A borrow parameter `p` lives in the caller's region of its argument. -/
def calleeTheta (d : Nat) (θ : Nat → Lbl) (Γ : Ctx) (ps args : List Nat) : Nat → Lbl := fun p =>
  match (ps.zip args).lookup p with
  | some a => (match Γ.roots.lookup a with
    | some r => lab d θ r
    | none => (0, 0))
  | none => (0, 0)

/-- Owned arguments become owned by the callee's parameters. -/
def calleePar (D : Decls) (d : Nat) (par : PMap) (ps args : List Nat) : PMap := fun b =>
  match par b with
  | .var l =>
    if l.1 = d ∧ isOwn D l.2 = true then
      (match (args.zip ps).lookup l.2 with
       | some p => .var (d + 1, p)
       | none => .var l)
    else .var l
  | q => q

/-- The callee's owned result becomes owned by `x`. -/
def retPar (d x ret : Nat) (par : PMap) : PMap := fun b =>
  match par b with
  | .var l => if l = (d + 1, ret) then .var (d, x) else .var l
  | q => q

theorem calleePar_fld {D d par ps args b b' k f} :
    calleePar D d par ps args b = .fld b' k f ↔ par b = .fld b' k f := by
  unfold calleePar
  constructor
  · intro h; split at h
    · split at h
      · split at h <;> cases h
      · cases h
    · rename_i q hq; exact h ▸ (by simp_all)
  · intro h; rw [h]

theorem calleePar_ch {D d par ps args b} (h : calleePar D d par ps args b ≠ par b) :
    ∃ a, par b = .var (d, a) ∧ isOwn D a = true ∧ a ∈ args := by
  unfold calleePar at h
  split at h
  · rename_i l hl
    split at h
    · rename_i hc
      split at h
      · rename_i p hp
        exact ⟨l.2, by rw [hl]; obtain ⟨l1, l2⟩ := l; simp at hc; rw [hc.1], hc.2,
          (List.of_mem_zip (lookup_zip_mem hp)).1⟩
      · exact absurd hl.symm h
    · exact absurd hl.symm h
  · exact absurd rfl h

theorem retPar_fld {d x ret par b b' k f} : retPar d x ret par b = .fld b' k f ↔ par b = .fld b' k f := by
  unfold retPar
  constructor
  · intro h; split at h
    · split at h <;> cases h
    · simp_all
  · intro h; rw [h]

theorem retPar_ch {d x ret par b} (h : retPar d x ret par b ≠ par b) : par b = .var (d + 1, ret) := by
  unfold retPar at h
  split at h
  · rename_i l hl; split at h
    · rename_i e; rw [hl, e]
    · exact absurd hl.symm h
  · exact absurd rfl h

theorem mono_fld {μ : Mem} {par par' : PMap} (h : ∀ c b k f, par' c = .fld b k f ↔ par c = .fld b k f)
    (hch : ∀ c, par' c ≠ par c → ∃ l, par c = .var l ∧ par' c ≠ .nil)
    (hH : ∀ b l, Anc par b l → ∃ blk, μ[b]? = some blk ∧ BlkOK P μ par b blk) :
    ∀ b l, Anc par' b l → ∃ blk, μ[b]? = some blk ∧ BlkOK P μ par' b blk := by
  intro b l ha
  rcases anc_coarse (N := fun _ => False) (fun c e _ => by
      obtain ⟨l, hl, -⟩ := hch c e; exact .inl ⟨l, .var hl⟩) (fun _ _ _ _ _ h => h) ha with ⟨l', ha'⟩ | h
  · obtain ⟨blk, hb, hok⟩ := hH b l' ha'
    exact ⟨blk, hb, hok.mono SidPres.refl fun c k f _ hc => (h c b k f).mpr hc⟩
  · exact h.elim

theorem callee_inv {fn : Fn} {args : List Nat} {Γm : Ctx} (hI : Inv P D d θ Γ ρ μ par)
    (hlen : args.length = fn.params.length)
    (hzip : ∀ p a, (p, a) ∈ fn.params.zip args → D.lookup a = fn.decls.lookup p)
    (hcm : consumeArgs Rules.full D Γ args = some Γm)
    (hall : ∀ a ∈ args, isOwn D a = true ∨ a ∈ Γm.live) (hnd : fn.params.Nodup) :
    Inv P fn.decls (d + 1) (calleeTheta d θ Γ fn.params args) fn.init (bindArgs ρ fn.params args) μ
      (calleePar D d par fn.params args) := by
  obtain ⟨cr, csub, cown⟩ := consumeArgs_spec args Γ Γm hcm
  have hK : ∀ c l, calleePar D d par fn.params args c ≠ par c → Anc par c l →
      l.1 = d ∧ isOwn D l.2 = true ∧ l.2 ∈ args := by
    intro c l e ha
    obtain ⟨a, h1, h2, h3⟩ := calleePar_ch e
    rw [Anc.det ha (.var h1)]; exact ⟨rfl, h2, h3⟩
  refine ⟨?_, ?_, ?_, ?_⟩
  · intro p
    unfold calleeTheta
    split
    · split
      · rename_i r _; cases r with
        | loc y => simp [lab]
        | ext q => have := hI.theta q; simp [lab]; omega
      · simp
    · simp
  · intro c b k f h; exact hI.dom c b k f (calleePar_fld.mp h)
  · refine mono_fld (fun c b k f => calleePar_fld) (fun c e => ?_) hI.heap
    obtain ⟨a, h1, -⟩ := calleePar_ch e
    refine ⟨_, h1, fun hn => ?_⟩
    unfold calleePar at hn; rw [h1] at hn; simp only at hn
    split at hn
    · split at hn <;> cases hn
    · cases hn
  · intro p hp
    have hp' : p ∈ fn.params := hp
    obtain ⟨a, ha⟩ := lookup_zip_some hlen hp'
    have hD := hzip p a (lookup_zip_mem ha)
    have hmem : a ∈ args := (List.of_mem_zip (lookup_zip_mem ha)).2
    have hρ : bindArgs ρ fn.params args p = ρ a := by simp [bindArgs, ha]
    unfold VOK; rw [hρ]
    cases hDp : fn.decls.lookup p with
    | none => trivial
    | some τ =>
      rw [hDp] at hD
      cases τ with
      | int =>
        have hno : isOwn D a = false := by simp [isOwn, hD]
        have ha' : a ∈ Γ.live := csub a ((hall a hmem).resolve_left (by simp [hno]))
        have hv := hI.vars a ha'; unfold VOK at hv; rw [hD] at hv; exact hv
      | own s =>
        have ho : isOwn D a = true := by simp [isOwn, hD]
        obtain ⟨g1, -, g3, -⟩ := cown a hmem ho
        have hv := hI.vars a g1; unfold VOK at hv; rw [hD] at hv
        rcases hv with h | ⟨b, h1, h2, h3⟩
        · exact .inl h
        · refine .inr ⟨b, h1, ?_, h3⟩
          unfold calleePar; rw [h2]; simp [ho, lookup_zip_inv hnd ha g3]
      | ref s =>
        have hno : isOwn D a = false := by simp [isOwn, hD]
        have hm : a ∈ Γm.live := (hall a hmem).resolve_left (by simp [hno])
        have hv := hI.vars a (csub a hm); unfold VOK at hv; rw [hD] at hv
        rcases hv with h | ⟨b, off, r, h1, h2, h3, h4⟩
        · exact .inl h
        · refine .inr ⟨b, off, .ext p, h1, h2, lookup_init' _ hp' (by simp [isRef, hDp]), ?_⟩
          have hθ : lab (d + 1) (calleeTheta d θ Γ fn.params args) (.ext p) = lab d θ r := by
            simp [lab, calleeTheta, ha, h3]
          rw [hθ]
          refine anc_keep hK h4 ?_
          cases r with
          | loc y =>
            rintro ⟨-, hy, hya⟩
            exact (cown y hya hy).2.2.2 a hm h3
          | ext q => intro h; have := hI.theta q; simp [lab] at h; omega

theorem lab_le {d θ} (hθ : ∀ p, (θ p).1 < d) (r : Root) : (lab d θ r).1 ≤ d := by
  cases r with
  | loc y => simp [lab]
  | ext p => have := hθ p; simp [lab]; omega

theorem caller_post {fn : Fn} {args : List Nat} {Γm Γf Γ1 : Ctx} {θe : Nat → Lbl} {ρ' : Env} {μ' : Mem}
    {pare' parc' : PMap} {x v}
    (hI : Inv P D d θ Γ ρ μ par) (hcm : consumeArgs Rules.full D Γ args = some Γm)
    (hFe : Frame (d + 1) (calleePar D d par fn.params args) pare') (hse : SidPres μ μ')
    (hIe' : Inv P fn.decls (d + 1) θe Γf ρ' μ' pare')
    (hfld : ∀ c b k f, parc' c = .fld b k f ↔ pare' c = .fld b k f)
    (hch : ∀ c, parc' c ≠ pare' c → ∃ l, pare' c = .var l ∧ parc' c ≠ .nil ∧ d < l.1)
    (hlive : ∀ z ∈ Γ1.live, z ≠ x → z ∈ Γm.live)
    (hroots : ∀ z, z ≠ x → Γ1.roots.lookup z = Γ.roots.lookup z)
    (hx : x ∈ Γ1.live → VOK D d θ Γ1.roots (ρ.set x v) μ' parc' x) :
    Good P D d θ μ par Γ1 (.ok (ρ.set x v) μ') := by
  obtain ⟨cr, csub, cown⟩ := consumeArgs_spec args Γ Γm hcm
  have hKe : ∀ c l, calleePar D d par fn.params args c ≠ par c → Anc par c l →
      l.1 = d ∧ isOwn D l.2 = true ∧ l.2 ∈ args := by
    intro c l e ha
    obtain ⟨a, h1, h2, h3⟩ := calleePar_ch e
    rw [Anc.det ha (.var h1)]; exact ⟨rfl, h2, h3⟩
  have hKc : ∀ c l, parc' c ≠ pare' c → Anc pare' c l → d < l.1 := by
    intro c l e ha
    obtain ⟨l', h1, -, h3⟩ := hch c e
    rw [Anc.det ha (.var h1)]; exact h3
  have keep : ∀ b l, Anc pare' b l → l.1 ≤ d → Anc parc' b l :=
    fun b l ha hl => anc_keep hKc ha (by omega)
  -- A region at depth at most `d` other than an owned argument's is the same for the caller after the call.
  have through : ∀ b l, Anc par b l → l.1 ≤ d → ¬ (l.1 = d ∧ isOwn D l.2 = true ∧ l.2 ∈ args) →
      Anc parc' b l :=
    fun b l ha hl hk => keep b l (hFe.anc (anc_keep hKe ha hk) (by omega)) hl
  refine ⟨parc', ⟨hI.theta, fun c b k f h => hIe'.dom c b k f ((hfld c b k f).mp h), ?_, ?_⟩, ?_, hse⟩
  · refine mono_fld hfld (fun c e => ?_) hIe'.heap
    obtain ⟨l, h1, h2, -⟩ := hch c e; exact ⟨l, h1, h2⟩
  · intro z hz
    by_cases e : z = x
    · subst e; exact hx hz
    have hzm := hlive z hz e
    have hzΓ := csub z hzm
    have hv := hI.vars z hzΓ
    unfold VOK at hv ⊢
    simp only [Env.set, e, ite_false]
    split at hv <;> rename_i hD
    · exact hv
    · rcases hv with h | ⟨b, h1, h2, h3⟩
      · exact .inl h
      · refine .inr ⟨b, h1, ?_, hse _ _ h3⟩
        have hnot : ¬ ((d, z).1 = d ∧ isOwn D (d, z).2 = true ∧ (d, z).2 ∈ args) := by
          rintro ⟨-, ho, hm⟩; exact (cown z hm ho).2.1 hzm
        have hpe : calleePar D d par fn.params args b = par b :=
          Classical.byContradiction fun ne => hnot (hKe b _ ne (.var h2))
        have hpe' : pare' b = .var (d, z) := by
          rw [hFe b (d, z) (.var (by rw [hpe]; exact h2)) (by simp), hpe, h2]
        exact Classical.byContradiction fun ne => by
          have := hKc b (d, z) (by rw [hpe']; exact ne) (.var hpe'); simp at this
    · rcases hv with h | ⟨b, off, r, h1, h2, h3, h4⟩
      · exact .inl h
      · refine .inr ⟨b, off, r, h1, hse _ _ h2, by rw [hroots z e]; exact h3, ?_⟩
        refine through b _ h4 (lab_le hI.theta r) ?_
        cases r with
        | loc y => rintro ⟨-, hy, hya⟩; exact (cown y hya hy).2.2.2 z hzm h3
        | ext q => intro h; have := hI.theta q; simp [lab] at h; omega
    · trivial
  · intro b l ha hl
    have hnk : ¬ (l.1 = d ∧ isOwn D l.2 = true ∧ l.2 ∈ args) := fun h => by omega
    have hpe : calleePar D d par fn.params args b = par b :=
      Classical.byContradiction fun ne => hnk (hKe b l ne ha)
    have hae := anc_keep hKe ha hnk
    have hpe' := hFe b l hae (by omega)
    have hc : parc' b = pare' b :=
      Classical.byContradiction fun ne => by have := hKc b l ne (hFe.anc hae (by omega)); omega
    rw [hc, hpe', hpe]

theorem live_init (hI : Inv P D d θ Γ ρ μ par) {z} (hz : z ∈ Γ.live) (hD : (D.lookup z).isSome = true) :
    ρ z ≠ .uninit := by
  have hv := hI.vars z hz
  unfold VOK at hv
  split at hv <;> rename_i h
  · obtain ⟨n, hn⟩ := hv; rw [hn]; simp
  · rcases hv with h | ⟨b, h1, -⟩ <;> simp [*]
  · rcases hv with h | ⟨b, off, -, h1, -⟩ <;> simp [*]
  · rw [h] at hD; cases hD

theorem fnOK_full {fn : Fn} (h : fnOK Rules.full P fn = true) :
    ∃ Γf, chkS Rules.full P fn.decls fn.init fn.body = some Γf ∧ fn.params.Nodup ∧ fn.ret ∈ Γf.live ∧
      (fn.decls.lookup fn.ret).isSome ∧
      ∀ s, fn.decls.lookup fn.ret = some (.ref s) → ∃ p, fn.bound = some p ∧ Γf.roots.lookup fn.ret = some (.ext p) := by
  unfold fnOK at h
  split at h
  · cases h
  rename_i Γf hb
  simp only [Bool.and_eq_true, decide_eq_true_eq] at h
  obtain ⟨⟨hn, hr⟩, hm⟩ := h
  refine ⟨Γf, hb, hn, hr, ?_, fun s hs => ?_⟩
  · split at hm <;> simp_all
  · rw [hs] at hm; simp only [Rules.full, ite_true] at hm
    split at hm
    · rename_i p hp
      simp only [Bool.and_eq_true, decide_eq_true_eq] at hm
      exact ⟨p, hp, hm.1.1⟩
    · cases hm

theorem case_call (hP : P.ok Rules.full = true) {x f args n}
    (IH : ∀ (D : Decls) d θ Γ Γ' ρ μ par s, chkS Rules.full P D Γ s = some Γ' → Inv P D d θ Γ ρ μ par →
      Good P D d θ μ par Γ' (exec P n ρ μ s))
    (hc : chkS Rules.full P D Γ (.call x f args) = some Γ') (hI : Inv P D d θ Γ ρ μ par) :
    Good P D d θ μ par Γ' (exec P (n + 1) ρ μ (.call x f args)) := by
  simp only [chkS] at hc
  cases hfn : P.fns[f]? with
  | none => simp [hfn] at hc
  | some fn =>
  simp only [hfn] at hc
  split at hc; rotate_left; · cases hc
  rename_i hz
  obtain ⟨hlen, hzip⟩ := hz
  have hzip' : ∀ p a, (p, a) ∈ fn.params.zip args → D.lookup a = fn.decls.lookup p ∧ (D.lookup a).isSome := by
    intro p a hm
    have := List.all_eq_true.mp hzip (p, a) hm
    simpa using this
  cases hcm : consumeArgs Rules.full D Γ args with
  | none => simp [hcm] at hc
  | some Γm =>
  rw [hcm] at hc; simp only [Rules.full, ite_true] at hc
  by_cases hall : (args.all fun a => isOwn D a || decide (a ∈ Γm.live)) = true
  rotate_left
  · simp [hall] at hc
  simp only [hall, ite_true] at hc
  have hall' : ∀ a ∈ args, isOwn D a = true ∨ a ∈ Γm.live := by
    intro a ha; have := List.all_eq_true.mp hall a ha; simpa using this
  obtain ⟨cr, csub, cown⟩ := consumeArgs_spec args Γ Γm hcm
  have hfnm : fn ∈ P.fns := List.mem_iff_getElem?.mpr ⟨f, hfn⟩
  obtain ⟨Γf, hb, hnd, hret, hretT, hretB⟩ := fnOK_full (List.all_eq_true.mp hP fn hfnm)
  have hinit : ∀ a ∈ args, ρ a ≠ .uninit := by
    intro a ha
    by_cases ho : isOwn D a = true
    · exact live_init hI (cown a ha ho).1 (by unfold isOwn at ho; split at ho <;> simp_all)
    · obtain ⟨p, hp⟩ := mem_zip_of_mem hlen ha
      exact live_init hI (csub a ((hall' a ha).resolve_left ho)) (hzip' p a hp).2
  have hany : (args.any fun a => decide (ρ a = .uninit)) = false := by
    simp only [List.any_eq_false, decide_eq_true_eq]; exact hinit
  simp only [exec, hfn, hany, Bool.false_eq_true, ite_false]
  have hIe := callee_inv (fn := fn) hI hlen (fun p a hm => (hzip' p a hm).1) hcm hall' hnd
  have hg := IH _ _ _ _ _ _ _ _ _ hb hIe
  cases he : exec P n (bindArgs ρ fn.params args) μ fn.body with
  | trap => exact trivial
  | outOfFuel => exact trivial
  | ub k => rw [he] at hg; exact hg.elim
  | ok ρ' μ' =>
  rw [he] at hg
  obtain ⟨pare', hIe', hFe, hse⟩ := hg
  have hv := hIe'.vars fn.ret hret
  have hri : ρ' fn.ret ≠ .uninit := live_init hIe' hret hretT
  simp only [hri, ite_false]
  split at hc <;> try cases hc
  · -- int result
    rename_i hT hDx
    refine caller_post (parc' := pare') hI hcm hFe hse hIe' (fun _ _ _ _ => Iff.rfl)
      (fun _ e => absurd rfl e) (fun z hz e => (mem_add.mp hz).resolve_left e) (fun _ _ => by simp [cr]) ?_
    intro _; unfold VOK at hv ⊢; rw [hT] at hv; rw [hDx]; simpa [Env.set] using hv
  · -- owned result
    rename_i s s' hT hDx
    split at hc <;> rename_i hss <;> cases hc
    subst hss
    refine caller_post (parc' := retPar d x fn.ret pare') hI hcm hFe hse hIe' (fun _ _ _ _ => retPar_fld)
      (fun c e => ⟨_, retPar_ch e, by unfold retPar; rw [retPar_ch e]; simp, by simp⟩)
      (fun z hz e => by
        rcases mem_assignOwn.mp hz with h | ⟨h, -⟩
        · exact absurd h e
        · exact h)
      (fun _ _ => by simp [cr]) ?_
    intro _; unfold VOK at hv ⊢; rw [hT] at hv; rw [hDx]
    rcases hv with h | ⟨b, h1, h2, h3⟩
    · exact .inl (by simp [Env.set, h])
    · exact .inr ⟨b, by simp [Env.set, h1], by simp [retPar, h2], h3⟩
  · -- borrowed result
    rename_i s s' hT hDx
    split at hc <;> rename_i hss <;> try cases hc
    subst hss
    obtain ⟨p, hbp, hrp⟩ := hretB s hT
    rw [hbp] at hc; simp only at hc
    cases ha : (fn.params.zip args).lookup p with
    | none => simp [ha] at hc
    | some a =>
    cases hr : Γ.roots.lookup a with
    | none => simp [ha, hr] at hc
    | some r =>
    simp only [ha, hr, Option.some.injEq] at hc; subst hc
    refine caller_post (parc' := pare') hI hcm hFe hse hIe' (fun _ _ _ _ => Iff.rfl)
      (fun _ e => absurd rfl e) (fun z hz e => (mem_addRef.mp hz).resolve_left e)
      (fun z e => by rw [addRef_lookup]; simp [e, cr]) ?_
    intro _; unfold VOK at hv ⊢; rw [hT] at hv; rw [hDx]
    rcases hv with h | ⟨b, off, rt, h1, h2, h3, h4⟩
    · exact .inl (by simp [Env.set, h])
    · rw [hrp] at h3; cases h3
      refine .inr ⟨b, off, r, by simp [Env.set, h1], h2, by rw [addRef_lookup]; simp, ?_⟩
      have : lab (d + 1) (calleeTheta d θ Γ fn.params args) (.ext p) = lab d θ r := by
        simp [lab, calleeTheta, ha, hr]
      rw [← this]; exact h4

/-! ## Control flow and the main theorem -/

theorem Good.weaken {Γa Γb : Ctx} {o : Out}
    (h : Good P D d θ μ par Γa o) (hw : ∀ ρ' μ' par', Inv P D d θ Γa ρ' μ' par' → Inv P D d θ Γb ρ' μ' par') :
    Good P D d θ μ par Γb o := by
  cases o with
  | ok ρ' μ' => obtain ⟨par', h1, h2, h3⟩ := h; exact ⟨par', hw _ _ _ h1, h2, h3⟩
  | _ => exact h

theorem Good.then {Γ1 : Ctx} {o : Out} {g : Env → Mem → Out}
    (h : Good P D d θ μ par Γ1 o)
    (hg : ∀ ρ1 μ1 par1, Inv P D d θ Γ1 ρ1 μ1 par1 → Frame d par par1 → SidPres μ μ1 →
      Good P D d θ μ par Γ' (g ρ1 μ1)) :
    Good P D d θ μ par Γ' (o.then g) := by
  cases o with
  | ok ρ1 μ1 => obtain ⟨par1, h1, h2, h3⟩ := h; exact hg ρ1 μ1 par1 h1 h2 h3
  | ub k => exact h.elim
  | _ => trivial

theorem Good.rebase {μ1 : Mem} {par1 : PMap} {o : Out} (h : Good P D d θ μ1 par1 Γ' o)
    (hF : Frame d par par1) (hs : SidPres μ μ1) : Good P D d θ μ par Γ' o := by
  cases o with
  | ok ρ' μ' => obtain ⟨par', h1, h2, h3⟩ := h; exact ⟨par', h1, hF.trans h2, hs.trans h3⟩
  | _ => exact h

end

theorem exec_good {P : Prog} (hP : P.ok Rules.full = true) : ∀ n,
    (∀ (D : Decls) d θ Γ Γ' ρ μ par s, chkS Rules.full P D Γ s = some Γ' → Inv P D d θ Γ ρ μ par →
      Good P D d θ μ par Γ' (exec P n ρ μ s)) ∧
    (∀ (D : Decls) d θ Γi Γ2 ρ μ par c body, chkE P D Γi c = true → chkS Rules.full P D Γi body = some Γ2 →
      sub D Γi Γ2 = true → Inv P D d θ Γi ρ μ par → Good P D d θ μ par Γi (exec P n ρ μ (.loop c body))) := by
  intro n
  induction n with
  | zero => exact ⟨fun _ _ _ _ _ _ _ _ _ _ _ => trivial, fun _ _ _ _ _ _ _ _ _ _ _ _ _ _ => trivial⟩
  | succ n ih =>
    obtain ⟨IH, IHL⟩ := ih
    have L : ∀ (D : Decls) d θ Γi Γ2 ρ μ par c body, chkE P D Γi c = true →
        chkS Rules.full P D Γi body = some Γ2 → sub D Γi Γ2 = true → Inv P D d θ Γi ρ μ par →
        Good P D d θ μ par Γi (exec P (n + 1) ρ μ (.loop c body)) := by
      intro D d θ Γi Γ2 ρ μ par c body hcE hb hs hI
      simp only [exec]
      rcases evalE_ok hI c hcE with h | ⟨z, h⟩
      · rw [h]; exact trivial
      rw [h]; simp only [Res.thenO]
      split
      · exact Good.inv hI
      · exact (IH D d θ Γi Γ2 ρ μ par body hb hI).then fun ρ1 μ1 par1 h1 h2 h3 =>
          (IHL D d θ Γi Γ2 ρ1 μ1 par1 c body hcE hb hs (h1.sub hs)).rebase h2 h3
    refine ⟨?_, L⟩
    intro D d θ Γ Γ' ρ μ par s hc hI
    cases s with
    | skip => simp only [chkS, Option.some.injEq] at hc; subst hc; exact Good.inv hI
    | seq a b =>
      simp only [chkS] at hc
      cases ha : chkS Rules.full P D Γ a with
      | none => simp [ha] at hc
      | some Γ1 =>
        simp only [ha, Option.bind_some] at hc
        simp only [exec]
        exact (IH D d θ Γ Γ1 ρ μ par a ha hI).then fun ρ1 μ1 par1 h1 h2 h3 =>
          (IH D d θ Γ1 Γ' ρ1 μ1 par1 b hc h1).rebase h2 h3
    | set x e => exact case_set hc hI
    | nul x => exact case_nul hc hI
    | borrow x y i => exact case_borrow hc hI
    | move x y => exact case_move hc hI
    | load x y i f => exact case_load hc hI
    | store y i f e => exact case_store hc hI
    | put y i f z => exact case_put hc hI
    | take x y i f => exact case_take hc hI
    | malloc x s e => exact case_malloc hc hI
    | free x => exact case_free hc hI
    | realloc x s e => exact case_realloc hc hI
    | ifz c a b =>
      simp only [chkS] at hc
      split at hc; rotate_left; · cases hc
      rename_i hcE
      cases ha : chkS Rules.full P D Γ a with
      | none => simp [ha] at hc
      | some Fa =>
      cases hb : chkS Rules.full P D Γ b with
      | none => simp [ha, hb] at hc
      | some Fb =>
      simp only [ha, hb, Option.bind_some, Option.some.injEq] at hc; subst hc
      simp only [exec]
      rcases evalE_ok hI c hcE with h | ⟨z, h⟩
      · rw [h]; exact trivial
      rw [h]; simp only [Res.thenO]
      split
      · exact (IH D d θ Γ Fa ρ μ par a ha hI).weaken fun _ _ _ h => h.meet_l
      · exact (IH D d θ Γ Fb ρ μ par b hb hI).weaken fun _ _ _ h => h.meet_r
    | ifNull x a b =>
      simp only [chkS] at hc
      split at hc; rotate_left; · cases hc
      rename_i hx
      obtain ⟨hx, hps⟩ := hx
      cases hps' : ptrS D x with
      | none => simp [hps'] at hps
      | some s =>
      cases ha : chkS Rules.full P D Γ a with
      | none => simp [ha] at hc
      | some Fa =>
      cases hb : chkS Rules.full P D Γ b with
      | none => simp [ha, hb] at hc
      | some Fb =>
      simp only [ha, hb, Option.bind_some, Option.some.injEq] at hc; subst hc
      simp only [exec]
      rcases live_ptr hI hx hps' with h | ⟨_, _, _, h, -⟩
      · rw [h]; exact (IH D d θ Γ Fa ρ μ par a ha hI).weaken fun _ _ _ h => h.meet_l
      · rw [h]; exact (IH D d θ Γ Fb ρ μ par b hb hI).weaken fun _ _ _ h => h.meet_r
    | loop c body =>
      simp only [chkS] at hc
      cases h1 : chkS Rules.full P D Γ body with
      | none => simp [h1] at hc
      | some Γ1 =>
      simp only [h1] at hc
      split at hc; rotate_left; · cases hc
      rename_i hcE
      cases h2 : chkS Rules.full P D (meet D Γ Γ1) body with
      | none => simp [h2] at hc
      | some Γ2 =>
      simp only [h2] at hc
      split at hc <;> rename_i hs <;> cases hc
      exact L D d θ _ Γ2 ρ μ par c body hcE h2 hs hI.meet_l
    | call x f args => exact case_call hP IH hc hI

/-- The main theorem: a program whose functions pass the checker, and whose
main statement passes it from the empty context, never reaches UB, for any
fuel. In particular it never reaches a use after free or a double free. -/
theorem own_sound (P : Prog) (hP : P.ok Rules.full = true) (D : Decls) (s : Stmt) (G : Ctx)
    (hc : chkS Rules.full P D Ctx.empty s = some G) (fuel : Nat) :
    (exec P fuel (fun _ => .uninit) [] s).isUB = false := by
  have hI : Inv P D 1 (fun _ => (0, 0)) Ctx.empty (fun _ => .uninit) [] (fun _ => .nil) :=
    ⟨fun _ => (by simp), fun _ _ _ _ h => (by cases h), fun _ _ h => absurd rfl (Anc.ne_nil h),
      fun _ h => (by simp [Ctx.empty] at h)⟩
  have hg := (exec_good hP fuel).1 D 1 _ _ G _ _ _ s hc hI
  revert hg
  cases exec P fuel (fun _ => .uninit) [] s <;> simp [Good, Out.isUB]

theorem own_no_uaf (P : Prog) (hP : P.ok Rules.full = true) (D : Decls) (s : Stmt) (G : Ctx)
    (hc : chkS Rules.full P D Ctx.empty s = some G) (fuel : Nat) :
    (exec P fuel (fun _ => .uninit) [] s).ubKind ≠ some .useAfterFree ∧
    (exec P fuel (fun _ => .uninit) [] s).ubKind ≠ some .doubleFree := by
  have := own_sound P hP D s G hc fuel
  revert this
  cases exec P fuel (fun _ => .uninit) [] s <;> simp [Out.isUB, Out.ubKind]

/-- The theorem applies to real programs: idiom 1 (the linked list) is safe for
every amount of fuel, not just the fuel `decide` ran it with. -/
theorem idiom1_sound (fuel : Nat) : (exec P0 fuel (fun _ => .uninit) [] listProg).isUB = false := by
  obtain ⟨G, hG⟩ := Option.isSome_iff_exists.mp
    (show (chkS Rules.full P0 lD .empty listProg).isSome = true by decide)
  exact own_sound P0 (by decide) lD listProg G hG fuel

end SafeC.Own
