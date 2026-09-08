# How Rust reaches CBMC

CBMC checks C. Kani checks Rust. Kani uses CBMC. The reconciliation is that
**CBMC's checking engine never sees C** — it sees GOTO, and GOTO is a
language-independent IR. C is just its most famous front end.

Kani is a `rustc` backend that emits GOTO instead of LLVM IR.

```
Rust source
    │  rustc front end (parse, typecheck, borrow-check, monomorphise)
    ▼
  MIR                          Rust's mid-level IR: basic blocks + terminators
    │  kani-compiler: MIR→MIR passes
    │    - reachability analysis from #[kani::proof] harnesses
    │    - insert Rust-semantics checks (overflow, alignment, validity, ...)
    ▼
  MIR (instrumented)
    │  codegen backend (cprover_bindings crate)
    ▼
  GOTO program  ──────►  CBMC  ──►  SAT/SMT
    │
    └─ kani-driver maps CBMC's property results back to Rust source locations
```

Kani hooks into `rustc` via `rustc_private` after MIR generation. MIR rather
than LLVM IR is a deliberate choice: MIR is already monomorphised and still
carries Rust's type information, enum discriminant layouts and validity
invariants. By LLVM IR those are gone, and with them any chance of checking
*Rust-level* undefined behaviour rather than C-level UB.

## Reproducing this

```bash
cargo install --locked kani-verifier && cargo kani setup
kani demo.rs
```

Kani ships its own pinned CBMC (0.67.0 bundles CBMC 6.8.0) in
`~/.kani/kani-<version>/bin/` — `cbmc`, `goto-cc`, `goto-instrument`,
`goto-analyzer`, plus `kissat` as the SAT solver. Goto binaries carry a format
version, so a system CBMC of a different version will refuse to read Kani's
output:

```
$ goto-instrument --show-goto-functions demo__...check_mid.out
The input was compiled with an unsupported version of goto-cc; please recompile
```

Use the bundled tools instead.

## The correspondence

`demo.rs` is the C midpoint example from the main tour, transliterated:

| C / CBMC | Rust / Kani |
|---|---|
| `nondet_uint()` | `kani::any::<u32>()` |
| `__CPROVER_assume(c)` | `kani::assume(c)` |
| `__CPROVER_assert(c, "…")` | `assert!(c)` |
| the `main` harness | `#[kani::proof] fn check_mid()` |
| `__CPROVER_requires` / `ensures` | `#[kani::requires]` / `#[kani::ensures]` |
| `--unwind N` | `#[kani::unwind(N)]` / `--default-unwind N` |

```
$ kani demo.rs
Check 1: mid.assertion.1
	 - Status: FAILURE
	 - Description: "attempt to add with overflow"
	 - Location: demo.rs:3:5 in function mid

Check 4: check_mid.assertion.1
	 - Status: SUCCESS
	 - Description: "assertion failed: lo <= m && m <= hi"

VERIFICATION:- FAILED
```

Note what changed relative to C. In C, `lo + hi` wrapping is defined behaviour
for `unsigned`, so CBMC reports the *user's* assertion failing. In Rust,
overflowing addition is a panic, so Kani reports `attempt to add with overflow`
at the arithmetic itself and the user's assertion passes — the overflow is
caught before it can produce a wrong midpoint. Same engine, different language
semantics, because the checks are inserted at the MIR level by Kani, not by
CBMC.

## The GOTO program, generated from Rust

`kani --keep-temps demo.rs` leaves the goto binary next to the source:

```bash
$ K=~/.kani/kani-0.67.0/bin
$ $K/goto-instrument --show-goto-functions demo__...check_mid.out
```

The harness (symbols de-mangled here for readability):

```
check_mid /* check_mid */
        DECL check_mid::1::var_1::lo : unsignedbv[32]
        DECL check_mid::1::var_2::hi : unsignedbv[32]
        DECL check_mid::1::var_5::m  : unsignedbv[32]
        CALL check_mid::1::var_1::lo := _RINvCs1xmhYfHElKw_4kani3anymE...()
     1: CALL check_mid::1::var_2::hi := _RINvCs1xmhYfHElKw_4kani3anymE...()
     2: ASSIGN check_mid::1::var_4 := cast(check_mid::1::var_1::lo ≤ check_mid::1::var_2::hi, c_bool[8])
        ASSUME check_mid::1::var_4 ≠ 0
     3: CALL check_mid::1::var_5::m := mid(check_mid::1::var_1::lo, check_mid::1::var_2::hi)
```

`kani::any()` is a `CALL` to a nondet-returning function; `kani::assume` is a
plain `ASSUME`. Identical to what `goto-cc` emits from C.

And `mid` itself, which is where the Rust semantics show up:

```
mid /* mid */
        DECL mid::1::temp_0 : struct tag-overflow_result_Unsignedbv { width: 32 }
        ASSIGN mid::1::temp_0 := overflow_result-+(mid::1::var_1::lo, mid::1::var_2::hi)
        ASSIGN mid::$tmp::tmp_statement_expression := { mid::1::temp_0.result,
                                                        cast(mid::1::temp_0.overflowed, c_bool[8]), … }
        ASSERT false                             // KANI_CHECK_ID_…::demo_1   (reachability marker)
        ASSERT ¬(mid::1::var_4.1 ≠ 0)            // attempt to add with overflow
        ASSUME ¬(mid::1::var_4.1 ≠ 0)
     1: ASSIGN mid::1::var_3 := mid::1::var_4.0
        ASSERT ¬(2 = 0)                          // attempt to divide by zero
        ASSUME ¬(2 = 0)
        ASSIGN … := mid::1::var_3 / 2
        SET RETURN VALUE mid::1::var_0
     3: END_FUNCTION
```

Three things worth reading closely:

- `u32` became `unsignedbv[32]`, the same bit-vector type C's `unsigned` maps
  to. Below GOTO there is no such thing as a Rust type.
- Rust's checked add became CBMC's `overflow_result-+` expression, whose result
  is a `{result, overflowed}` struct — CBMC already had this because C needs
  `--signed-overflow-check`. Kani reuses the primitive and attaches a Rust
  panic message to it.
- The `ASSERT … ASSUME …` pair is the standard "check it, then assume it holds"
  idiom, so one failure doesn't poison every downstream property. The bare
  `ASSERT false` above it is Kani's reachability marker: if CBMC can prove it
  unreachable, the check below is dead code and Kani reports it as unreachable
  rather than passing.

## CBMC really is just reading a goto binary

Run the stock `cbmc` binary on Kani's output — no Kani, no Rust toolchain
involved in this step:

```
$ ~/.kani/kani-0.67.0/bin/cbmc demo__...check_mid.out --unwind 1
** Results:
demo.rs function check_mid
[check_mid.assertion.1] line 12 […] assertion failed: lo <= m && m <= hi: SUCCESS
[check_mid.reachability_check.1] line 12 …: FAILURE

demo.rs function mid
[mid.assertion.1] line 3 […] attempt to add with overflow: FAILURE
[mid.assertion.2] line 3 […] attempt to divide by zero: SUCCESS

** 4 of 7 failed (3 iterations)
VERIFICATION FAILED
```

It even resolves source locations back to `demo.rs`, because Kani wrote Rust
file/line information into the GOTO `source_locationt`s. What `kani-driver`
adds on top is presentation: filtering the reachability markers, de-mangling
symbols, and turning `[mid.assertion.1]` into a readable Rust-level report.

## Other consumers

Because the goto binary is a file format and not an API, a third tool can pick
it up. `goto-transcoder` converts CBMC-format goto binaries into ESBMC's
format, so a Kani-produced GOTO program can be verified by ESBMC's SMT and
k-induction engines instead of CBMC's SAT engine — Rust in, a completely
different checker out, with no Rust front end on the ESBMC side.
