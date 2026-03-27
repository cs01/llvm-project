# Flow-Nullability Architecture Review Guide

A written walkthrough of the flow-sensitive null pointer dereference checker for code reviewers and contributors.

See also: [flow-nullability-architecture.md](flow-nullability-architecture.md) for Mermaid diagrams of the same concepts.

---

## What This Is

A flow-sensitive null pointer dereference checker integrated into Clang's standard warning system — the same infrastructure that powers `-Wthread-safety` and `-Wuninitialized`. It catches things like `*p` when `p` might be null, by tracking which pointers have been null-checked through the control flow graph.

Here's the simplest example of what it does:

```c
void example(int * _Nullable p) {
    *p;          // WARNING: dereference of nullable pointer

    if (p) {
        *p;      // OK — p was null-checked, analysis knows it's non-null here
    }

    *p;          // WARNING — outside the if, p could still be null
}
```

The analysis understands control flow. It doesn't just look at whether a null check exists *somewhere* — it tracks *where* in the program's execution each pointer is known to be non-null.

---

## The Three-Layer Architecture

### Layer 1: The Analysis Engine

**`clang/lib/Analysis/FlowNullability.cpp`** (~1,100 lines)

This is the brain. It's a forward dataflow analysis — meaning it walks the control flow graph (CFG) from the function entry to the exit, propagating facts about which pointers are known non-null.

**Key concept: per-edge state tracking.** Most dataflow analyses track state per-block. This one tracks state per-edge (`EdgeStates[{PredBlockID, SuccBlockID}]`). Why? Consider:

```c
void example(int * _Nullable p) {
    if (p) {
        // On the TRUE edge from the if-block: NarrowedVars = {p}
        *p;   // OK — p is narrowed
    } else {
        // On the FALSE edge from the if-block: NarrowedVars = {}
        *p;   // WARNING — p is not narrowed here
    }
}
```

The `if (p)` block has two outgoing edges. A per-block analysis would give it one state — but the true and false edges need *different* states. Per-edge tracking (`EdgeStates[{PredBlockID, SuccBlockID}]`) solves this. This is the single most important architectural decision in the whole thing.

#### The core loop (`runFlowNullabilityAnalysis`)

1. Initialize a worklist with the CFG entry block
2. Dequeue a block
3. Compute entry state by intersecting all predecessor edge states (narrowed only if ALL paths agree)
4. Run transfer functions on each statement in the block (this is where warnings get emitted)
5. At the terminator (`if`/`while`/`for`), split into TrueState/FalseState based on the condition
6. Propagate to successor edges; if state changed, re-enqueue the successor
7. Repeat until fixpoint (no more changes)

Here's what that looks like on real code:

```c
void loop_example(int * _Nullable p) {
    while (p) {          // Terminator: splits into true/false edges
        *p;              // OK — inside loop body, p is narrowed (true edge)
        p = get_next();  // Transfer function: assignment INVALIDATES narrowing of p
                         // State changed → re-enqueue this block
    }
    // After loop (false edge): p is NOT narrowed
    *p;                  // WARNING
}
```

The worklist processes the loop body twice: once to discover the narrowing, once to reach the fixpoint after the reassignment invalidates it.

#### NullState tracks six sets

| Set | What it tracks | Example trigger |
|-----|---------------|-----------------|
| `NarrowedVars` | Local vars/params proven non-null | `if (p)` on true edge, `p = &x`, `p = new int` |
| `NarrowedMembers` | `var->field` pairs proven non-null | `if (obj->ptr)` on true edge |
| `NarrowedThisMembers` | `this->field` proven non-null | `if (this->data_)` on true edge |
| `NullableVars` | Vars known to be nullable | `p = nullptr`, `p = nullable_func()` |
| `NullableThisMembers` | Member smart ptrs with nullability evidence | `sp_.reset()`, `std::move(sp_)` |
| `BoolGuards` | Bool vars assigned from null checks | `bool valid = (p != nullptr)` |

#### Intersect semantics (`intersect` function)

At merge points (after if/else rejoins), narrowing uses **intersection** (conservative — only keep if ALL paths agree), while nullability uses **union** (any path can make it nullable).

```c
void merge_example(int * _Nullable p, int * _Nullable q, bool cond) {
    if (cond) {
        if (!p) return;  // narrows p after this point
        if (!q) return;  // narrows q after this point
        // NarrowedVars = {p, q}
    } else {
        if (!p) return;  // narrows p after this point
        // NarrowedVars = {p}
    }
    // MERGE POINT: intersection of {p, q} and {p} = {p}
    *p;  // OK — both paths narrowed p
    *q;  // WARNING — only one path narrowed q
}
```

For nullability it's the opposite — union, because if *any* path makes a pointer nullable, it might be null at the merge:

```c
void nullable_merge(int * _Nonnull p, bool cond) {
    if (cond) {
        p = nullptr;  // NullableVars = {p}
    } else {
        // NullableVars = {}
    }
    // MERGE: union of {p} and {} = {p}
    *p;  // WARNING — p could be null (the cond==true path set it to null)
}
```

#### Transfer functions (the `TransferFunctions` class)

Each statement type gets its own handler. Concrete examples:

**DeclStmt** — initialization determines starting nullability:
```c
int *a = get_nullable();   // a is nullable (came from nullable source)
int *b = &local_var;       // b is narrowed (address-of is never null)
int *c = new int(42);      // c is narrowed (new never returns null)
int * _Nonnull d = get();  // d is narrowed (declared _Nonnull)
```

**BinaryOperator** — assignment invalidates then potentially re-narrows:
```c
void assign_example(int * _Nullable p) {
    if (p) {
        *p;            // OK — narrowed
        p = other();   // INVALIDATES narrowing of p
        *p;            // WARNING — p was reassigned, no longer narrowed
        p = &local;
        *p;            // OK — &local is never null, re-narrowed
    }
}
```

**UnaryOperator** — `*p` triggers the warning check, `p++` invalidates:
```c
void unary_example(int * _Nullable p) {
    if (p) {
        *p;    // OK — narrowed
        p++;   // INVALIDATES narrowing (pointer arithmetic)
        *p;    // WARNING — p was modified
    }
}
```

**MemberExpr** — arrow dereference with smart pointer special-casing:
```c
struct Node { int val; Node * _Nullable next; };

void member_example(Node * _Nullable n) {
    n->val;           // WARNING — n could be null
    if (n) {
        n->val;       // OK — n is narrowed
        n->next->val; // WARNING — n->next could be null
    }
}
```

**CallExpr** — `_Nonnull` params narrow, smart pointer methods track nullability:
```c
void takes_nonnull(int * _Nonnull p);

void call_example(int * _Nullable p) {
    takes_nonnull(p);  // After this call, p is narrowed
                       // (if it weren't non-null, calling would be UB)
    *p;                // OK

    std::unique_ptr<int> sp = std::make_unique<int>(42);
    *sp;               // OK — just created
    sp.reset();        // marks sp as nullable (NullableThisMembers)
    *sp;               // WARNING — sp could be null after reset()
}
```

#### Condition analysis (`analyzeCondition`)

Figures out what a branch condition tells us about nullability. Every pattern, with examples:

```c
void conditions(int * _Nullable p, std::unique_ptr<int> sp) {
    // Boolean truthiness
    if (p) { *p; }               // OK — p narrowed on true edge

    // Null comparisons
    if (p != nullptr) { *p; }    // OK
    if (p == nullptr) {} else { *p; }  // OK — narrowed on false edge

    // Negation
    if (!p) { return; }
    *p;                          // OK — early return means p must be non-null here

    // Smart pointer operator bool()
    if (sp) { *sp; }             // OK — sp narrowed

    // __builtin_expect / LIKELY / UNLIKELY — unwrapped to find the real condition
    if (__builtin_expect(p != nullptr, 1)) { *p; }  // OK
}
```

**Boolean intermediary tracking** — when a bool is assigned from a null-comparison, the analysis remembers the relationship and uses it when the bool is later tested:

```c
void bool_intermediary(int * _Nullable p) {
    bool valid = (p != nullptr);    // BoolGuards: valid → (p, non-negated)
    if (valid) {
        *p;                         // OK — tracked through bool guard
    }
    // Also works: bool isNull = (p == nullptr); if (!isNull) { *p; }
    // Also works: bool ok = p; if (ok) { *p; }
    // Guards are invalidated if either the bool or the pointer is reassigned.
}
```

**Negated conjunction decomposition** (`!(p && q)`) — the CFG merges `&&` operand paths before the `if`-decision, losing per-variable narrowing at the merge. `analyzeCondition` detects `!(A && B)` and recursively decomposes the `&&`, narrowing ALL operands on the false edge (where `&&` was true → all pointers non-null):

```c
void negated_and(int * _Nullable p, int * _Nullable q, int * _Nullable r) {
    if (!(p && q)) return;
    *p;  // OK — decomposeAnd extracts both p and q
    *q;  // OK

    // Works with any depth: !(a && b && c) narrows all three
    if (!(p && q && r)) return;
    *r;  // OK
}
```

This is logically equivalent to `!p || !q` (De Morgan), which the CFG handles natively. The fix makes `!(p && q)` match that behavior.

#### `getTerminalCondition`

This is subtle. The CFG decomposes `p && q` into separate blocks, but the terminator expression is the full `p && q`. This function follows the RHS chain to find the leaf being tested in each block:

```c
void and_chain(int * _Nullable p, int * _Nullable q) {
    if (p && q) {
        // CFG splits this into two blocks:
        //   Block 1: tests p (getTerminalCondition finds p)
        //     → true edge: NarrowedVars += {p}
        //   Block 2: tests q (getTerminalCondition finds q)
        //     → true edge: NarrowedVars += {q}
        *p;  // OK — narrowed by first condition
        *q;  // OK — narrowed by second condition
    }
}
```

Without `getTerminalCondition`, the analysis would see `p && q` as the condition for *both* blocks and wouldn't know which variable to narrow for which block.

---

### Layer 2: The Glue (Sema Integration)

**`clang/lib/Sema/AnalysisBasedWarnings.cpp`** (lines ~3047–3310)

This wires the analysis into Clang's existing "analysis-based warnings" infrastructure — the same place that ThreadSafety and UninitializedValues live.

- **`FlowNullabilityReporter`** — implements the `FlowNullabilityHandler` callback interface. When the analysis calls `handleNullableDereference()`, this converts it into a `S.Diag()` call (Clang's diagnostic emission). That's what produces the actual compiler output:

  ```
  foo.c:10:5: warning: dereference of nullable pointer
              [-Wflow-nullable-dereference]
      *p;
      ^~
  ```

- **`shouldEnableFlowNullability`** — checks whether analysis is enabled for this specific function (via the `FlowSensitiveNullabilityEnabled` flag set in Layer 3).

- **CFG build integration** (lines 3222–3231): When flow-nullability is enabled, it requests `setAllAlwaysAdd()` on the CFG builder, which means ALL statements become visible as CFGElements. Without this, some statements get optimized away in the CFG. For example:

  ```c
  void example(int * _Nullable p) {
      int x = *p;  // Without setAllAlwaysAdd(), the CFG might fold this
                    // DeclStmt and the deref into a single element,
                    // and the analysis would miss the dereference.
  }
  ```

---

### Layer 3: Gradual Adoption Gating

**`clang/lib/Sema/SemaDecl.cpp`**

Sets `FlowSensitiveNullabilityEnabled` per-function in `ActOnStartOfFunctionDef`. The analysis only fires when:

1. The function is inside `#pragma clang assume_nonnull begin`/`end`, **OR**
2. `-fnullability-default=nullable|nonnull` is set (not `unspecified`)

This means you can adopt incrementally:

```c
// File: legacy_code.c — no pragma, no flags → ZERO analysis overhead

// File: new_module.c
#pragma clang assume_nonnull begin

void safe_function(int *p) {
    // Analysis runs here! Unannotated 'int *' is treated as _Nonnull
    // because we're inside assume_nonnull.
}

#pragma clang assume_nonnull end

void unprotected_function(int *p) {
    // Analysis does NOT run here — outside the pragma region
    *p;  // No warning, even if p could be null
}
```

Or adopt globally with a flag:

```bash
# Every unannotated pointer in the entire TU is treated as _Nullable
clang -fflow-sensitive-nullability -fnullability-default=nullable foo.c
```

---

## Suppressions — What Doesn't Warn

Not everything that *looks* like a null deref triggers a warning. Key suppressions:

```cpp
struct Foo {
    int x;
    void method() {
        this->x;        // Suppressed — 'this' is never null in C++
        (*this).x;      // Suppressed — same reason
    }
};

void smart_ptr_example(std::unique_ptr<int> &sp) {
    sp->do_thing();     // Suppressed — CXXOperatorCallExpr(OO_Arrow)
                        // Smart pointer operator-> is suppressed at the call site
                        // because it's too noisy otherwise
}

void call_doesnt_invalidate(int * _Nullable p) {
    if (p) {
        some_function(); // Does NOT invalidate p's narrowing
        *p;              // Still OK — pointers are passed by value in C,
                         // so a function call can't change a local pointer variable
    }
}
```

---

## Diagnostics & Flags

| File | What it defines |
|------|----------------|
| `DiagnosticSemaKinds.td` | Warning `warn_strict_nullability_dereference` ("dereference of nullable pointer") + fix note `note_nullable_dereference_fix` |
| `DiagnosticGroups.td` | `-Wflow-nullable-dereference` (the warning), `-Wflow-nullability` (parent group) |
| `Options.td` + `Driver/ToolChains/Clang.cpp` | Three flags (see below) |

### Compiler flags

| Flag | Purpose | Example |
|------|---------|---------|
| `-fflow-sensitive-nullability` | Master switch (LangOpt) | `clang -fflow-sensitive-nullability foo.c` |
| `-fnullability-default=nullable\|nonnull\|unspecified` | What unannotated pointers mean | `-fnullability-default=nullable` treats `int *` as `int * _Nullable` |
| `-fstrict-nullability-inference` | Treat inferred nullability as explicit | Makes the analysis stricter about inferred types |

---

## How It Compares to Similar Analyses

This follows the exact same architectural pattern as:

- **ThreadSafety** (`lib/Analysis/ThreadSafety.cpp`) — forward analysis, handler callbacks, wired via AnalysisBasedWarnings
- **UninitializedValues** (`lib/Analysis/UninitializedValues.cpp`) — same pattern

**Key difference**: ThreadSafety and UninitializedValues use per-block state. FlowNullability uses **per-edge state** for more precise branch refinement. This is a valid and well-known technique in dataflow analysis literature — it's called "edge-based dataflow" or "conditional constant propagation style" tracking.

The practical impact: per-block can't distinguish true/false branches from the *same* block, so it can't narrow `if (p)` correctly. Per-edge can.

---

## Architecture Questions Worth Investigating

### 1. Fixpoint convergence guarantee

The worklist iterates until no edge states change. Why does this always terminate?

The lattice is finite: each variable can only be in narrowed/nullable/neither. The intersect function is monotone (it can only shrink the narrowed set, never grow it). So the state can only change a bounded number of times before stabilizing.

### 2. Scaling with function size

The analysis is O(blocks x vars x iterations). How many fixpoint iterations happen in practice?

Usually 2–3 for structured code. But consider:

```c
void pathological(int * _Nullable p) {
    for (int i = 0; i < 100; i++) {
        if (i % 2) p = get_ptr();  // reassignment
        else if (p) use(p);        // narrowing
    }
    // Each iteration could change the state, but in practice
    // the worklist converges quickly because the narrowing sets
    // are bounded by the number of variables in the function.
}
```

### 3. Smart pointer special-casing

Lines 130–203 hardcode `std::unique_ptr`, `std::shared_ptr`, `std::weak_ptr` by string name. This works but is brittle — does upstream Clang have precedent for this? Yes — ThreadSafety does similar things with lock types (`std::mutex`, `std::shared_lock`, etc.).

### 4. Function calls don't invalidate narrowing

This is a deliberate design choice:

```c
void example(int * _Nullable p) {
    if (p) {
        some_function(p);  // Does NOT invalidate p's narrowing
        *p;                // Still OK
    }
}
```

Pointers are passed by value in C/C++, so `some_function` receives a *copy* of `p`. It can't change the local variable `p`. This is correct for local pointers — but could theoretically miss aliasing through globals or double-pointers. This is the right tradeoff for a non-path-sensitive analysis.

### 5. No interprocedural analysis

The analysis doesn't look inside callees:

```c
// Even if get_data() always returns non-null in practice,
// the analysis only sees the return type annotation.
int * _Nullable get_data();

void caller() {
    int *p = get_data();
    *p;  // WARNING — return type says _Nullable
}
```

This is standard for Sema-level warnings. The static analyzer (Clang SA) handles interprocedural analysis but is much more expensive.

### 6. The `NullableThisMembers` set — evidence-based approach

Only warn on `this->sp_->x` if the current function has evidence that `sp_` could be null:

```cpp
class Widget {
    std::unique_ptr<Impl> impl_;

    void good_method() {
        impl_->do_thing();  // NO WARNING — no evidence impl_ could be null
                            // (without this heuristic, every class with a
                            // unique_ptr member would false-positive on every method)
    }

    void method_with_evidence() {
        impl_.reset();         // This gives evidence that impl_ can be null
        // ...
        impl_->do_thing();     // WARNING — we saw reset() in this function,
                               // so impl_ could be null
    }
};
```

---

## File Map for Review

| File | Role |
|------|------|
| `include/clang/Analysis/Analyses/FlowNullability.h` | Public API (handler + entry point) |
| `lib/Analysis/FlowNullability.cpp` | The analysis (~1,100 lines) |
| `lib/Sema/AnalysisBasedWarnings.cpp` | Wiring into Sema (~60 lines changed) |
| `lib/Sema/SemaDecl.cpp` | Per-function gating |
| `lib/Sema/Sema.cpp` | Helper functions |
| `include/clang/Sema/Sema.h` | `FlowSensitiveNullabilityEnabled` flag |
| `include/clang/Basic/DiagnosticSemaKinds.td` | Diagnostic definition |
| `include/clang/Basic/DiagnosticGroups.td` | Warning group |
| `include/clang/Driver/Options.td` | Flag definitions |
| `lib/Driver/ToolChains/Clang.cpp` | Driver flag forwarding |
| `test/Sema/flow-nullability-*.cpp/.c` | 38 lit tests |
