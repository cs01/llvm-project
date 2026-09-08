# CBMC: how it works, and how to drive it

A worked tour of the C Bounded Model Checker. Everything in this document was
run against **CBMC 5.95.1** with the MiniSat 2.2.1 backend; the transcripts are
real output, not sketches.

```
sudo apt-get install cbmc      # gets cbmc, goto-cc, goto-instrument, ...
cbmc --version
```

---

## 1. The one-paragraph version

CBMC does not run your program and it does not approximate it. It **translates
your program into a single logical formula** whose satisfying assignments are
exactly the executions that violate a property, then hands that formula to a
SAT or SMT solver. If the solver says *unsatisfiable*, no such execution exists
(within the bound). If it says *satisfiable*, the satisfying assignment **is**
the bug, and CBMC decodes it back into a concrete input and a step-by-step
trace.

The word "bounded" is the catch: loops are unrolled a finite number of times.
Everything else — integer wraparound, pointer arithmetic, struct padding, union
type-punning, `memcpy` — is modelled bit-precisely.

---

## 2. Your first proof

`examples/01_first.c`:

```c
int abs_val(int x) {
  if (x < 0)
    return -x;
  return x;
}

int main(void) {
  int x;                    // uninitialised == nondeterministic
  assert(abs_val(x) >= 0);
}
```

An uninitialised local is not "garbage" to CBMC — it is a symbol that ranges
over *every* `int`. So:

```
$ cbmc examples/01_first.c --trace
...
Solving with MiniSAT 2.2.1 with simplifier
255 variables, 535 clauses
SAT checker: instance is SATISFIABLE

** Results:
[main.assertion.1] line 11 assertion abs_val(x) >= 0: FAILURE

Trace for main.assertion.1:
State 11 ... x=-2147483648 (10000000 00000000 00000000 00000000)
...
Violated property:
  assertion abs_val(x) >= 0
  return_value_abs_val >= 0

VERIFICATION FAILED
```

`-INT_MIN` is `INT_MIN`. A fuzzer finds this eventually; CBMC finds it because
it is the only satisfying assignment of a formula it solved exhaustively.

Ask for the underlying reason and it names it directly:

```
$ cbmc examples/01_first.c --signed-overflow-check
[abs_val.overflow.1] line 5 arithmetic overflow on signed unary minus in -x: FAILURE
```

---

## 3. The pipeline

```
   .c / .cpp / .go / .jimple
            │   ansi-c / cpp / jsil front end   (parse, typecheck)
            ▼
      symbol table                          symbolt: name, type, value
            │   goto-conversion
            ▼
    GOTO program  ◄──────────────  goto-cc emits this as a "goto binary"
            │                      goto-instrument rewrites it here
            │   instrumentation (bounds, pointer, overflow, ... checks
            │                    inserted as explicit ASSERT instructions)
            ▼
    symbolic execution (goto_symext)   loops unwound, calls inlined,
            │                          every assignment renamed to SSA
            ▼
  symex_target_equationt              a list of SSA_stept: assignments,
            │                         assumptions, assertions, guards
            │   flattening (bit-blasting or SMT encoding)
            ▼
   CNF / SMT-LIB  ──►  MiniSat / CaDiCaL / Z3 / CVC5
            │
            ▼
    UNSAT → property holds        SAT → decode model → goto_tracet → counterexample
```

Each arrow is inspectable from the command line. That is the best way to learn
the tool.

### 3a. GOTO programs: control flow becomes explicit

Structured control flow is gone. There are 19 instruction types plus a
sentinel (`src/goto-programs/goto_program.h`):

```
GOTO  ASSUME  ASSERT  OTHER  SKIP  START_THREAD  END_THREAD  LOCATION
END_FUNCTION  ATOMIC_BEGIN  ATOMIC_END  SET_RETURN_VALUE  ASSIGN  DECL
DEAD  FUNCTION_CALL  THROW  CATCH  INCOMPLETE_GOTO  NO_INSTRUCTION_TYPE
```

A function is a `std::list` of those, each with a guard, a source location, and
targets. Look at a loop:

```
$ goto-cc -o 03.goto examples/03_loop.c
$ goto-instrument --show-goto-functions 03.goto
sum_to /* sum_to */
        DECL sum_to::1::s : signedbv[32]
        ASSIGN sum_to::1::s := 0
        DECL sum_to::1::1::i : unsignedbv[32]
        ASSIGN sum_to::1::1::i := cast(0, unsignedbv[32])
     1: IF ¬(sum_to::1::1::i ≤ sum_to::n) THEN GOTO 2
        ASSIGN sum_to::1::s := cast(cast(sum_to::1::s, unsignedbv[32]) + sum_to::1::1::i, signedbv[32])
        ASSIGN sum_to::1::1::i := sum_to::1::1::i + 1
        GOTO 1
     2: SKIP
        DEAD sum_to::1::1::i
        SET RETURN VALUE sum_to::1::s
     3: END_FUNCTION
```

Note `signedbv[32]` / `unsignedbv[32]` and the explicit `cast`. Types are
bit-vectors from here on; C's integer promotion rules have already been made
explicit. This is why CBMC catches wraparound bugs that source-level reasoning
misses.

### 3b. Symbolic execution: the SSA equation

`goto_symext` walks the GOTO program along all paths at once, renaming every
assignment (`x` → `x#1`, `x#2`, …) and merging branches with a *phi* select.
The result is the `symex_target_equationt` — a flat list of `SSA_stept`s, each
carrying a `guard` (its path condition), a `type` (assignment / assume / assert
/ constraint / goto), and its expressions.

`--show-vcc` prints exactly what goes to the solver. For
`examples/05_tiny.c`:

```c
int a = nondet_int();
int b = a + 1;
if (b > a) b = b - a;
__CPROVER_assert(b == 1, "b is one");
```

```
$ cbmc examples/05_tiny.c --show-vcc
{-6}  main::$tmp::return_value_nondet_int!0@1#2 = nondet_symbol identifier="symex::nondet0"
{-7}  main::1::a!0@1#2 = main::$tmp::return_value_nondet_int!0@1#2
{-8}  main::1::b!0@1#2 = main::1::a!0@1#2 + 1
{-9}  goto_symex::\guard#1 ⇔ ¬(main::1::a!0@1#2 ≥ main::1::b!0@1#2)
{-10} main::1::b!0@1#3 = main::1::b!0@1#2 + -main::1::a!0@1#2
{-11} main::1::b!0@1#4 = (goto_symex::\guard#1 ? main::1::b!0@1#3 : main::1::b!0@1#2)
├──────────────────────────
{1}   main::1::b!0@1#4 = 1
```

Everything above the line is the *antecedent* (the program), the line below is
the *goal*. CBMC asks the solver for a model of `antecedent ∧ ¬goal`. `!0` is
the thread id, `@1` the call-stack frame, `#n` the SSA version — that naming
scheme (`ssa_exprt`'s three levels) is worth internalising, it shows up all over
traces and internals.

The variable name in the counterexample, `symex::nondet0`, is where the
*inputs* live. Decoding a SAT model back to those symbols is exactly how CBMC
produces test inputs.

Related switches: `--program-only` (the whole equation, including the CPROVER
library preamble), `--show-goto-functions`, `--show-symbol-table`,
`--smt2 --outfile x.smt2` (dump SMT-LIB instead of solving).

---

## 4. The modelling API (what you write in C)

This is the part people mean by "CBMC's API" most of the time. These are
intrinsics the front end understands; guard them with `#ifdef __CPROVER__` if
the file must also compile normally.

### Nondeterminism — the inputs of your proof

```c
int      nondet_int(void);     unsigned nondet_uint(void);
size_t   nondet_size_t(void);  char     nondet_char(void);
/* any undeclared function named nondet_* / __VERIFIER_nondet_* works,
   and so does simply reading an uninitialised variable */
```

### Constraints and properties

```c
__CPROVER_assume(cond);          // prune executions; NOT a runtime check
__CPROVER_assert(cond, "text");  // the property to prove
assert(cond);                    // same thing, via <assert.h>
__CPROVER_precondition(c, "m");  // assert here, assume at call sites
__CPROVER_cover(cond);           // ask: is this reachable?
```

`__CPROVER_assume` is the single most important one and the easiest to misuse.
It does not check anything — it *deletes* executions. `__CPROVER_assume(0)`
makes every property pass vacuously. Whenever a proof succeeds suspiciously
fast, check your assumptions with `--cover` or by asserting `0` at the point of
interest.

`examples/02_harness.c` is the shape almost every real CBMC proof takes:

```c
unsigned mid(unsigned lo, unsigned hi) { return (lo + hi) / 2; }

int main(void) {                       // the "proof harness"
  unsigned lo = nondet_uint();         // 1. symbolic input
  unsigned hi = nondet_uint();
  __CPROVER_assume(lo <= hi);          // 2. precondition
  unsigned m = mid(lo, hi);            // 3. call the code under test
  __CPROVER_assert(lo <= m && m <= hi, // 4. postcondition
                   "midpoint lies within the range");
}
```

```
[main.assertion.1] midpoint lies within the range: FAILURE
  lo=2351860939u  hi=3252661098u        # lo+hi wraps
```

`examples/02b_fixed.c` uses `lo + (hi - lo) / 2` and reports
`VERIFICATION SUCCESSFUL` — and that "successful" is a proof over all 2^64
input pairs, not a sample.

### Memory and pointer intrinsics

Useful inside assumptions and assertions:

```c
__CPROVER_is_fresh(p, size)         // p points to a freshly allocated block
__CPROVER_r_ok(p, size)             // readable
__CPROVER_w_ok(p, size)             // writable
__CPROVER_rw_ok(p, size)
__CPROVER_same_object(p, q)
__CPROVER_POINTER_OBJECT(p)         // object id
__CPROVER_POINTER_OFFSET(p)         // byte offset within the object
__CPROVER_OBJECT_SIZE(p)
__CPROVER_DYNAMIC_OBJECT(p)
__CPROVER_forall { int i; range ==> pred }    // quantifiers
__CPROVER_exists { int i; range ==> pred }
```

### Other useful intrinsics

```c
__CPROVER_array_equal(a, b);  __CPROVER_array_copy(dst, src);
__CPROVER_havoc_object(p);    // forget everything known about *p
__CPROVER_atomic_begin(); __CPROVER_atomic_end();
__CPROVER_input("name", v);  __CPROVER_output("name", v);
```

---

## 5. The built-in checks

CBMC's other big use is as an exhaustive sanitiser. These insert `ASSERT`
instructions during instrumentation; nothing about your source changes.

| Flag | Catches |
|---|---|
| `--bounds-check` | array index out of bounds |
| `--pointer-check` | NULL / invalid / dead / out-of-object dereference |
| `--pointer-primitive-check` | invalid arguments to the pointer intrinsics |
| `--pointer-overflow-check` | pointer arithmetic overflow |
| `--signed-overflow-check`, `--unsigned-overflow-check` | arithmetic wraparound |
| `--conversion-check` | lossy integer conversions |
| `--div-by-zero-check` | division / modulo by zero |
| `--undefined-shift-check` | shift by ≥ width or negative |
| `--float-overflow-check`, `--nan-check` | IEEE float issues |
| `--memory-leak-check`, `--memory-cleanup-check` | leaks at exit |
| `--enum-range-check` | enum-typed value outside its declared range |
| `--unwinding-assertions` | **loop bound too small** (see §6) |

There is no single "all checks" switch in 5.95 — list the ones you want.
`--malloc-fail-null` / `--malloc-fail-assert` control whether `malloc` is
allowed to fail in the model, which changes a surprising number of results.

`examples/04_memory.c` has an off-by-one `malloc`:

```c
char *dst = malloc(n);   // no +1 for the NUL
memcpy(dst, src, n);
dst[n] = '\0';           // one past the end
```

```
$ cbmc examples/04_memory.c --pointer-check --bounds-check --memory-leak-check --unwind 6
[dup_prefix.pointer_dereference.5] line 7 dereference failure:
    pointer outside object bounds in dst[(signed long int)n]: FAILURE
```

Note it also checked `memcpy`'s own preconditions (overlap, readable source,
writable destination) and `free`'s (dynamic object, offset zero, no double
free) — CBMC ships models of libc that carry those contracts.

---

## 6. The bound, and the one flag that keeps you honest

This is the concept that decides whether a CBMC result means anything.

`examples/03_loop.c` has a deliberate `i <= n` off-by-one, and a harness with
`n <= 4`, so the loop runs up to 6 times.

```
$ cbmc examples/03_loop.c --unwind 3
[main.assertion.1] assertion sum_to(n) == (int)(n*(n+1)/2): SUCCESS
VERIFICATION SUCCESSFUL          # ← a lie
```

Three unwindings is not enough to reach `n = 4`, so the executions that would
break the assertion were silently cut off. Add one flag:

```
$ cbmc examples/03_loop.c --unwind 3 --unwinding-assertions
[main.assertion.1] ... : SUCCESS
[sum_to.unwind.0] line 5 unwinding assertion loop 0: FAILURE
VERIFICATION FAILED              # ← honest: "my bound was too small"

$ cbmc examples/03_loop.c --unwind 7 --unwinding-assertions
[sum_to.unwind.0] line 5 unwinding assertion loop 0: SUCCESS
VERIFICATION SUCCESSFUL          # ← now this is a real proof
```

**`VERIFICATION SUCCESSFUL` without `--unwinding-assertions` means "no bug
found within the bound", not "no bug".** Always pass it, or prove that you do
not need it with loop contracts (§8).

Bound control:

```
--unwind N                  global bound
--unwindset f.0:8,f.1:3     per loop (function.loop-number)
--depth N                   bound on symex steps instead
--partial-loops             explore prefixes of loops (unsound, no assertion)
--no-unwinding-assertions   explicitly opt out
```

---

## 7. Selecting and reporting properties

Every check is a named property; you can enumerate and target them.

```
$ cbmc examples/04_memory.c --pointer-check --show-properties
Property main.precondition_instance.1:
  file 04_memory.c line 16 function main
  free argument must be NULL or valid pointer
  (void *)p == NULL || R_OK((void *)p, (unsigned long int)0)
...

$ cbmc examples/04_memory.c --pointer-check --unwind 6 \
      --property dup_prefix.pointer_dereference.5
[dup_prefix.pointer_dereference.5] ...: FAILURE
** 1 of 1 failed
```

One property at a time is often dramatically faster than all of them, and it is
the natural unit for parallelising a big proof across a CI matrix.

Useful reporting flags: `--trace` (counterexample), `--trace-hex`,
`--stop-on-fail`, `--verbosity N`, `--flush`.

### Coverage and test generation

Turn the checker around: instead of asking "can this assertion fail", ask "can
this branch be taken" — and the satisfying assignments are test inputs.

```
$ cbmc examples/03_loop.c --cover branch --unwind 7
** coverage results:
[main.coverage.1]   function main entry point: SATISFIED
[sum_to.coverage.1] function sum_to entry point: SATISFIED
[sum_to.coverage.2] line 5 block 2 branch false: SATISFIED
[sum_to.coverage.3] line 5 block 2 branch true: SATISFIED
** 4 of 4 covered (100.0%)
```

Criteria: `location`, `branch`, `decision`, `condition`, `mcdc`, `path`,
`assertion`, `cover`. Add `--xml-ui`/`--json-ui` and each `SATISFIED` comes with
the input values that get you there.

---

## 8. Beating the bound: function and loop contracts

BMC cannot prove anything about a loop whose trip count depends on an unbounded
input. Contracts replace a function with a specification and a loop with an
inductive invariant, which makes the proof *modular* and *unbounded*.

`examples/07_contract.c`:

```c
int twice(int x)
  __CPROVER_requires(x >= 0 && x < 1000)          // caller must guarantee
  __CPROVER_ensures(__CPROVER_return_value == 2 * x)   // callee promises
  __CPROVER_assigns()                             // frame condition (optional)
{ return x + x; }

int main(void) {
  int y = nondet_int();
  __CPROVER_assume(y >= 0 && y < 1000);
  int r = twice(y);
  __CPROVER_assert(r >= y, "result at least input");
}
```

Contracts are applied by `goto-instrument`, in two independent directions.

**Enforce** — prove the *body* satisfies the contract (assume `requires`, run
the body, assert `ensures`):

```
$ goto-cc -o 07.goto examples/07_contract.c
$ goto-instrument --dfcc main --enforce-contract twice 07.goto 07e.goto
$ cbmc 07e.goto
[twice.postcondition.1] Check ensures clause of contract contract::twice for function twice: SUCCESS
[twice.no_recursive_call.1] No recursive call to function twice when checking contract twice: SUCCESS
[main.assigns.3] Check that return_value_twice is assignable: SUCCESS
** 0 of 39 failed
VERIFICATION SUCCESSFUL
```

Break the body (`examples/07b_contract_violated.c` returns `x + x + 1`) and the
postcondition is the property that fails:

```
[twice.postcondition.1] Check ensures clause of contract contract::twice for function twice: FAILURE
```

**Replace** — at call sites, *use* the contract instead of the body (assert
`requires`, havoc the `assigns` set, assume `ensures`). The body is never
symbolically executed, so its loops are never unwound:

```
$ goto-instrument --dfcc main --replace-call-with-contract twice 07.goto 07r.goto
$ cbmc 07r.goto
[main.assertion.1] result at least input: SUCCESS
** 0 of 46 failed
VERIFICATION SUCCESSFUL
```

That is the whole point, and `examples/07c_unbounded_loop.c` makes it concrete —
same contract, but the body is now a loop:

```c
{ int r = 0; for (int i = 0; i < x; i++) r += 2; return r; }
```

```
# with contract replacement — no --unwind at all, and it still proves
$ goto-instrument --dfcc main --replace-call-with-contract twice 07c.goto 07cr.goto
$ cbmc 07cr.goto
[main.assertion.1] result at least input: SUCCESS
VERIFICATION SUCCESSFUL

# plain BMC on the same code needs a bound, and honestly reports it is too small
$ cbmc 07c.goto --unwind 5 --unwinding-assertions
[main.assertion.1]   result at least input: SUCCESS
[twice.unwind.0]     unwinding assertion loop 0: FAILURE
VERIFICATION FAILED
```

Loop contracts get you the same leverage inside a function you must keep:

```c
while (i < n)
  __CPROVER_loop_invariant(i <= n)
  __CPROVER_decreases(n - i)        // termination measure (optional)
{ i++; }
```

```bash
goto-instrument --dfcc main --apply-loop-contracts in.goto out.goto
```

Three practical notes from running all of the above:

- `--dfcc <harness-fn>` selects *dynamic frame condition checking*, the
  supported contracts path in current CBMC. The older non-DFCC invocation
  (`goto-instrument --enforce-contract f`) is deprecated; on 5.95.1 it crashed
  with an invariant violation on the loop-invariant example here.
- The harness function you name must exist **and the contracted function must be
  reachable from it**, or `goto-instrument` drops the function and CBMC then
  verifies nothing at all. `**** WARNING: no body for function ...` followed by
  a cheerful `VERIFICATION SUCCESSFUL` is the tell.
- Contract instrumentation is expensive: it links in a large
  `__CPROVER_contracts_*` runtime that is itself verified (note 39 and 46
  properties above for a four-line function). The pointer-heavy example in
  `examples/06_contract.c` — `__CPROVER_is_fresh` plus a loop invariant over
  pointers — did not finish in several minutes here. Budget accordingly, and
  reach for `--property`, `--object-bits` and slicing.

`goto-synthesizer` will attempt to *infer* loop invariants for you:

```bash
goto-synthesizer --dump-loop-contracts in.goto
```

---

## 9. The tool family

`cbmc` is one front end over a shared library. The rest are worth knowing:

| Tool | What it does |
|---|---|
| `goto-cc` / `goto-gcc` / `goto-ld` | gcc-compatible drop-in that emits **goto binaries** instead of objects. `make CC=goto-cc` builds a whole project into a model. |
| `goto-instrument` | The Swiss army knife on goto binaries: `--show-goto-functions`, `--show-symbol-table`, `--slice-global-inits`, `--drop-unused-functions`, all the `--*-check` instrumentations, contracts, `--unwind`, `--dump-c` (turn a goto binary back into C). |
| `goto-analyzer` | Abstract interpretation instead of BMC: `--vsd` (value-set domain), `--show-intervals`, `--verify`, `--simplify`. Cheap, unbounded, incomplete. Good for pre-simplifying a model before BMC. |
| `goto-harness` | Auto-generates proof harnesses (`--harness-type call-function`) including nondet structs and arrays. |
| `goto-diff` | Compares two goto binaries; the basis for incremental/differential verification. |
| `goto-synthesizer` | Infers loop invariants. |
| `memory-analyzer` | Snapshots a running process (via gdb) into a goto model, so you can start verification from real runtime state. |
| `crangler` | Source-to-source rewriting for making code verifiable (e.g. removing `static`). |
| `symtab2gb` | Builds a goto binary from a JSON symbol table — the entry point for **other language front ends** (this is how Kani, the Rust verifier, feeds CBMC). |

Typical whole-project flow:

```bash
make CC=goto-cc CXX=goto-c++ LD=goto-ld          # build a model of everything
goto-instrument --drop-unused-functions all.goto slim.goto
cbmc slim.goto --function my_harness --unwind 10 --unwinding-assertions \
     --pointer-check --bounds-check
```

---

## 10. Programmatic APIs

Three levels, in increasing order of commitment.

### 10a. Structured output — `--json-ui` / `--xml-ui`

This is the API most tooling should use. It is stable, needs no build, and
works with every tool in the family.

```
$ cbmc examples/02_harness.c --json-ui --trace > out.json
```

The output is a JSON array of objects; the interesting keys are
`messageText`/`messageType` (progress), `program`, `result`, and a final
`cProverStatus`:

```json
{ "result": [ {
    "property": "main.assertion.1",
    "description": "midpoint lies within the range",
    "status": "FAILURE",
    "sourceLocation": { "file": "02_harness.c", "function": "main", "line": "17" },
    "trace": [ ... ]
} ] }
{ "cProverStatus": "failure" }
```

```python
import json, subprocess
out = subprocess.run(["cbmc", "prog.c", "--json-ui", "--trace"],
                     capture_output=True, text=True).stdout
for elem in json.loads(out):
    for r in elem.get("result", []):
        if r["status"] == "FAILURE":
            print(r["property"], r["description"], r["sourceLocation"])
```

Exit codes: `0` verification successful, `10` verification failed, `6` parse or
usage error, `1` internal error. Do not parse the human-readable output.

### 10b. `libcprover-cpp` — the embedding API

CBMC ships a C++ library that exposes the whole pipeline as a session object.
Header: `src/libcprover-cpp/api.h`.

> Note: the Debian/Ubuntu `cbmc` package ships **binaries only** — no headers or
> `libcprover.a`. To use this API you must build CBMC from source
> (`cmake -S . -B build && cmake --build build`), which produces
> `libcprover-cpp` alongside the tools.

The surface is deliberately small:

```cpp
struct api_sessiont
{
  api_sessiont();
  explicit api_sessiont(const api_optionst &options);
  ~api_sessiont();

  void set_message_callback(api_message_callbackt cb, api_call_back_contextt ctx);

  void load_model_from_files(const std::vector<std::string> &files) const;
  void read_goto_binary(std::string &file) const;
  bool is_goto_binary(std::string &file) const;

  void drop_unused_functions() const;
  void validate_goto_model() const;

  std::unique_ptr<verification_resultt> verify_model() const;   // preprocess + run
  std::unique_ptr<verification_resultt> run_verifier() const;   // run only

  std::unique_ptr<std::string> get_api_version() const;
private:
  std::unique_ptr<api_session_implementationt> implementation;  // pimpl
  bool preprocess_model() const;
};
```

Options are a small fluent builder:

```cpp
class api_optionst {
public:
  static api_optionst create();
  api_optionst &simplify(bool);
  api_optionst &drop_unused_functions(bool);
  api_optionst &validate_goto_model(bool);
  std::unique_ptr<optionst> to_engine_options() const;
};
```

Results are property-indexed, not a blob of text:

```cpp
enum class prop_statust { NOT_CHECKED, UNKNOWN, NOT_REACHABLE, PASS, FAIL, ERROR };
enum class verifier_resultt { UNKNOWN, PASS, FAIL, ERROR };

struct verification_resultt {
  verifier_resultt final_result() const;
  std::vector<std::string> get_property_ids() const;
  std::string  get_property_description(const std::string &id) const;
  prop_statust get_property_status(const std::string &id) const;
};
```

Messages arrive through a C-style callback so the API never owns your logging:

```cpp
using api_call_back_contextt = void *;
using api_message_callbackt =
  void (*)(const api_messaget &message, api_call_back_contextt context);

const char *api_message_get_string(const api_messaget &);
bool        api_message_is_error(const api_messaget &);
```

See `api/example.cpp` in this directory for a complete program.

What `verify_model()` actually does, from `api.cpp` — this is the pipeline of
§3 in code, and reading it is the fastest way to understand CBMC's internals:

```cpp
void api_sessiont::load_model_from_files(const std::vector<std::string> &files) const
{
  implementation->model = std::make_unique<goto_modelt>(initialize_goto_model(
    files, *implementation->message_handler, *implementation->options));
}

bool api_sessiont::preprocess_model() const
{
  remove_asm(*model, *message_handler);            // 1. drop inline asm
  link_to_library(*model, *mh, cprover_c_library_factory);  // 2. add libc models
  if(::process_goto_program(*model, *options, log)) return true;
                                                   // 3. remove function ptrs,
                                                   //    vtables, returns, ...
  add_failed_symbols(model->symbol_table);         // 4. pointer-analysis prep
  label_properties(*model);                        // 5. name every assertion
  remove_skip(*model);
  return false;
}

std::unique_ptr<verification_resultt> api_sessiont::run_verifier() const
{
  all_properties_verifier_with_trace_storaget<multi_path_symex_checkert>
    verifier(*options, ui_message_handler, *model);
  auto results = verifier();                       // symex + solve
  ...
}
```

`multi_path_symex_checkert` is the classic CBMC engine (one equation, all paths
at once). Swapping it for `single_path_symex_checkert` gives you the
CBMC-as-symbolic-executor mode (`--paths`), one path per query.

There is also **`libcprover-rust`** (crate `libcprover_rust`), a `cxx`-based
binding over the same C++ surface. Build it with `CBMC_LIB_DIR` and
`CBMC_INCLUDE_DIR` pointing at your CBMC build.

### 10c. Linking against CBMC's internals

If you are writing a new analysis rather than driving CBMC, you work with these
directly. The vocabulary:

| Class | Role |
|---|---|
| `irept` | The universal node: an id (`irep_idt`, an interned string), a list of sub-nodes, and a map of named sub-nodes. **Everything** in CBMC is an `irept`. |
| `exprt` / `typet` | `irept` subclasses for expressions and types. `signedbv_typet`, `pointer_typet`, `plus_exprt`, `symbol_exprt`, … |
| `symbolt`, `symbol_tablet`, `namespacet` | Program symbols and lookup. |
| `goto_programt` (`instructiont`) | One function as a list of the 20 instruction types. |
| `goto_functionst`, `goto_modelt` | All functions + the symbol table. This is what a goto binary serialises. |
| `goto_symext` | The symbolic execution engine. |
| `symex_target_equationt`, `SSA_stept` | The SSA equation. |
| `decision_proceduret`, `prop_convt`, `boolbvt` | Solver interface; `boolbvt` is the bit-blaster. |
| `goto_tracet` | The decoded counterexample. |
| `messaget` / `message_handlert` | Logging; the API callback plugs in here. |

The Doxygen for all of this is generated from the tree
(`make -C doc doxygen`) and published at
<https://diffblue.github.io/cbmc/>.

---

## 11. Solver backends

```
--sat-solver minisat2|cadical|glucose|...    (built-in, default MiniSat2)
--z3 | --cvc5 | --bitwuzla                   (external SMT binary on $PATH)
--smt2 --outfile problem.smt2                (dump, solve elsewhere)
--refine                                     CEGAR over bit-vector arithmetic
--slice-formula                              drop equation steps the goal can't reach
--object-bits N                              bits reserved for pointer object ids
```

Rules of thumb: SAT (default) wins on bit-twiddling and small fixed arrays; SMT
wins on large arrays and heavy linear arithmetic; `--refine` helps when
multiplication or division dominates. `--object-bits` matters when you allocate
many objects — the default 8 caps you at 256 distinct objects, and CBMC will
tell you when you exceed it.

---

## 12. What CBMC does not do

- **Termination.** Loops are bounded; `--unwinding-assertions` tells you the
  bound was too small, but proving a loop terminates needs `__CPROVER_decreases`.
- **Unbounded loops without contracts.** See §8.
- **Anything outside the model.** System calls, unmodelled libraries and inline
  asm are stubbed or havocked. If a function has no body, CBMC assumes it can
  return anything and (without a contract) writes nothing.
- **Concurrency, cheaply.** It supports threads (`--mm sc|tso|pso`) but the
  interleaving explosion is real.
- **Scale.** Path count and formula size grow fast. Decomposition — small
  harnesses over single functions, contracts at the boundaries — is the entire
  practical skill.

---

## 13. Running the examples

```bash
sudo apt-get install cbmc
cd cbmc-tour
./run.sh            # runs every example and prints the output shown above
```

```
cbmc-tour/
├── README.md                        this document
├── run.sh                           runs sections 2-10 end to end (~1 min)
├── examples/
│   ├── 01_first.c                   -INT_MIN; nondeterministic locals
│   ├── 02_harness.c                 the proof-harness shape (buggy midpoint)
│   ├── 02b_fixed.c                  the same, proved
│   ├── 03_loop.c                    loop bounds and --unwinding-assertions
│   ├── 04_memory.c                  malloc off-by-one; memory-safety checks
│   ├── 05_tiny.c                    minimal program for reading --show-vcc
│   ├── 06_contract.c                advanced: is_fresh + pointer loop invariant
│   ├── 07_contract.c                requires/ensures, enforce and replace
│   ├── 07b_contract_violated.c      body that breaks its own ensures
│   └── 07c_unbounded_loop.c         contract replacement beats the bound
└── api/
    ├── drive_json.py                driving cbmc via --json-ui (runnable)
    ├── example.cpp                  libcprover-cpp embedding (needs a source build)
    └── CMakeLists.txt               how to link example.cpp against CBMC
```

## 14. Where to go next

- CBMC manual: <https://www.cprover.org/cprover-manual/>
- Source and Doxygen: <https://github.com/diffblue/cbmc>,
  <https://diffblue.github.io/cbmc/>
- `cbmc --help`, `goto-instrument --help`, and `man cbmc` — the man pages
  shipped by the package are complete and worth reading once end to end.
- AWS's `aws-c-common` CBMC proofs are the best public example of the harness +
  contract style at scale.
- Kani (Rust) and JBMC (Java) are the same engine with different front ends.
