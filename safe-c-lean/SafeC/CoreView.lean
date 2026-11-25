-- A function that changes a `_View` with one pointer through `struct v *` is `x = f(x)`: a result inside
-- the argument (rule 9), assigned to the variable passed. Nothing is added to the model; these check that
-- it accepts the shape and rejects what must be rejected. (Scalars such a function returns are `havoc`.)
import SafeC.CoreCall
namespace SafeC.Core

/-- `void next(struct it *i) { i->cur = i->cur->next; }`: parameter 0 is the pointer on entry. -/
def viewNext : Fn := ⟨[.shr], .bor 0, 2, .seq (.step 1 .shr 0 0) (.ret (.ptr 1))⟩

/-- `i->cur = other;` with `other` another parameter. -/
def viewOther : Fn := ⟨[.shr, .shr], .bor 0, 3, .seq (.borrow 2 .shr 1) (.ret (.ptr 2))⟩

/-- An owner `0`, `1 = begin(0)`, `next(&1)`, then `after`. -/
def viewUse (after : Cmd) : Fn :=
  ⟨[], .unit, 3, .seq (.new 0) (.seq (.borrow 1 .shr 0) (.seq (.call (.p 1) .shr 0 [1]) (.seq after (.ret .unit))))⟩

/-- `T *_Owned parse(struct it *i)`: the new object comes back in a box (parameter 1) of its own. -/
def viewParse : Fn :=
  ⟨[.shr, .excl], .bor 0, 4, .seq (.step 2 .shr 0 0) (.seq (.new 3) (.seq (.store 1 0 3) (.ret (.ptr 2))))⟩

/-- An owner 0, a view 1 of it, a box 2, the parsed object 3. -/
def parseUse (after : Cmd) : Fn :=
  ⟨[], .unit, 4, .seq (.new 0) (.seq (.borrow 1 .shr 0) (.seq (.new 2)
    (.seq (.call (.p 1) .shr 0 [1, 2]) (.seq (.take 3 2 0) (.seq (.free 2) (.seq after (.ret .unit)))))))⟩

theorem parse_ok : check [viewParse, parseUse (.seq (.free 0) (.free 3))] = true := by decide
theorem parse_twice : check [viewParse, parseUse (.seq (.free 3) (.free 3))] = false := by decide
theorem parse_stale : check [viewParse, parseUse (.seq (.free 0) (.read 0 1))] = false := by decide
theorem view_ok : check [viewNext, viewUse (.seq (.read 0 1) (.free 0))] = true := by decide
theorem view_stale : check [viewNext, viewUse (.seq (.free 0) (.read 0 1))] = false := by decide
theorem view_taken : check [viewNext, viewUse (.seq (.take 2 0 0) (.read 0 1))] = false := by decide
theorem view_other : check [viewOther] = false := by decide

end SafeC.Core
