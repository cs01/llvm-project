-- Calls through a function pointer parameter. The checker reads a signature, never a body, so a call
-- that may reach any of several functions with one signature is an `ite` over direct calls whose
-- checks agree. The `ite` itself is not written out here.
import SafeC.CoreCall
namespace SafeC.Core

theorem callK_sig (R : Rules) (P : Prog) (np : Nat) (d : Dest) (m : Mode) (f f' : Nat)
    (args : List Nat) (g : St) {fn fn' : Fn} (hf : P[f]? = some fn) (hf' : P[f']? = some fn')
    (hp : fn.params = fn'.params) (hr : fn.rt = fn'.rt) :
    callK R P np d m f args g = callK R P np d m f' args g := by
  simp only [callK, hf, hf', hp, hr]

end SafeC.Core
