# Flow-Nullability Architecture

Visual guide to the flow-sensitive null pointer dereference checker, integrated into Clang's standard analysis-based warning system.

---

## System Overview

How the three layers connect — from compiler flags down to warning output.

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'primaryColor': '#4a90d9', 'primaryTextColor': '#fff', 'lineColor': '#666', 'fontSize': '14px'}}}%%
graph TB
    subgraph INPUT["🔧 User Input"]
        FLAGS["-fflow-sensitive-nullability<br>-fnullability-default=nullable"]
        PRAGMA["#pragma clang assume_nonnull"]
        SOURCE["Source code with pointers"]
    end

    subgraph LAYER3["🚦 Layer 3: Gating — SemaDecl.cpp"]
        GATE["ActOnStartOfFunctionDef()<br>Sets FlowSensitiveNullabilityEnabled"]
    end

    subgraph LAYER2["🔌 Layer 2: Glue — AnalysisBasedWarnings.cpp"]
        SHOULD["shouldEnableFlowNullability()"]
        CFG_BUILD["Build CFG with<br>setAllAlwaysAdd()"]
        REPORTER["FlowNullabilityReporter<br>implements FlowNullabilityHandler"]
        DIAG["S.Diag() → compiler warning"]
    end

    subgraph LAYER1["🧠 Layer 1: Analysis Engine — FlowNullability.cpp"]
        ENTRY["runFlowNullabilityAnalysis()"]
        WORKLIST["ForwardDataflowWorklist"]
        TRANSFER["TransferFunctions"]
        CONDITION["analyzeCondition()"]
        CALLBACK["handler.handleNullableDereference()"]
    end

    FLAGS --> GATE
    PRAGMA --> GATE
    SOURCE --> CFG_BUILD
    GATE --> SHOULD
    SHOULD -->|enabled| CFG_BUILD
    CFG_BUILD --> ENTRY
    ENTRY --> WORKLIST
    WORKLIST --> TRANSFER
    WORKLIST --> CONDITION
    TRANSFER -->|"⚠️ nullable deref found"| CALLBACK
    CALLBACK --> REPORTER
    REPORTER --> DIAG

    classDef input fill:#6c5ce7,stroke:#5a4bd1,color:#fff,stroke-width:2px
    classDef gating fill:#fdcb6e,stroke:#f39c12,color:#2d3436,stroke-width:2px
    classDef glue fill:#74b9ff,stroke:#0984e3,color:#2d3436,stroke-width:2px
    classDef engine fill:#55efc4,stroke:#00b894,color:#2d3436,stroke-width:2px
    classDef warning fill:#ff7675,stroke:#d63031,color:#fff,stroke-width:2px

    class FLAGS,PRAGMA,SOURCE input
    class GATE gating
    class SHOULD,CFG_BUILD,REPORTER glue
    class ENTRY,WORKLIST,TRANSFER,CONDITION,CALLBACK engine
    class DIAG warning

    style INPUT fill:#6c5ce720,stroke:#6c5ce7,stroke-width:2px,color:#6c5ce7
    style LAYER3 fill:#fdcb6e20,stroke:#f39c12,stroke-width:2px,color:#f39c12
    style LAYER2 fill:#74b9ff20,stroke:#0984e3,stroke-width:2px,color:#0984e3
    style LAYER1 fill:#55efc420,stroke:#00b894,stroke-width:2px,color:#00b894
```

---

## The Worklist Algorithm

The core fixpoint iteration loop (`runFlowNullabilityAnalysis`, lines 880–1009).

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'primaryColor': '#dfe6e9', 'lineColor': '#636e72', 'fontSize': '14px'}}}%%
flowchart TD
    START([🚀 Start]) --> INIT["Initialize:<br>• Create empty EdgeStates map<br>• Narrow _Nonnull params in InitState<br>• Enqueue CFG entry block"]

    INIT --> DEQUEUE{"Dequeue block<br>from worklist"}
    DEQUEUE -->|"empty"| DONE([✅ Done — fixpoint reached])
    DEQUEUE -->|"got block"| MERGE["Compute entry state:<br>intersect all predecessor<br>edge states"]

    MERGE --> HAS_PRED{"Any predecessor<br>states found?"}
    HAS_PRED -->|no| DEQUEUE
    HAS_PRED -->|yes| RUN_TF["Run TransferFunctions<br>on each statement in block<br>(may emit warnings)"]

    RUN_TF --> SPLIT["Split into TrueState / FalseState<br>based on terminator condition<br>(analyzeCondition)"]

    SPLIT --> PROPAGATE["For each successor edge:<br>• True edge → TrueState<br>• False edge → FalseState<br>• Unconditional → State"]

    PROPAGATE --> CHANGED{"Edge state<br>changed?"}
    CHANGED -->|yes| ENQUEUE["Enqueue successor block"]
    CHANGED -->|no| NEXT["Next successor"]
    ENQUEUE --> NEXT
    NEXT --> DEQUEUE

    classDef startend fill:#00b894,stroke:#00a381,color:#fff,stroke-width:2px
    classDef process fill:#74b9ff,stroke:#0984e3,color:#2d3436,stroke-width:2px
    classDef decision fill:#fdcb6e,stroke:#f39c12,color:#2d3436,stroke-width:2px
    classDef action fill:#a29bfe,stroke:#6c5ce7,color:#fff,stroke-width:2px
    classDef loop fill:#fd79a8,stroke:#e84393,color:#fff,stroke-width:2px

    class START,DONE startend
    class INIT,MERGE,RUN_TF,SPLIT,PROPAGATE process
    class DEQUEUE,HAS_PRED,CHANGED decision
    class ENQUEUE,NEXT action
```

---

## Per-Edge State Tracking

Why edges instead of blocks — the key architectural decision.

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'lineColor': '#636e72', 'fontSize': '14px'}}}%%
graph TB
    subgraph CFG["CFG for: if (p) { *p; } else { ... }"]
        ENTRY_BLK["📥 Entry Block"]
        IF_BLK["🔀 if (p)"]
        TRUE_BLK["✅ *p  ← safe, p is narrowed"]
        FALSE_BLK["❌ else  ← p could be null"]
        JOIN["🔗 Join point"]
    end

    ENTRY_BLK --> IF_BLK

    IF_BLK -->|"true edge<br>NarrowedVars += {p}"| TRUE_BLK
    IF_BLK -->|"false edge<br>NarrowedVars unchanged"| FALSE_BLK

    TRUE_BLK --> JOIN
    FALSE_BLK --> JOIN

    classDef entry fill:#74b9ff,stroke:#0984e3,color:#2d3436,stroke-width:2px
    classDef branch fill:#fdcb6e,stroke:#f39c12,color:#2d3436,stroke-width:2px
    classDef safe fill:#55efc4,stroke:#00b894,color:#2d3436,stroke-width:2px,stroke-dasharray:0
    classDef unsafe fill:#ff7675,stroke:#d63031,color:#fff,stroke-width:2px
    classDef join fill:#dfe6e9,stroke:#b2bec3,color:#2d3436,stroke-width:2px

    class ENTRY_BLK entry
    class IF_BLK branch
    class TRUE_BLK safe
    class FALSE_BLK unsafe
    class JOIN join

    style CFG fill:#2d343608,stroke:#636e72,stroke-width:2px,color:#2d3436

    linkStyle 1 stroke:#00b894,stroke-width:3px
    linkStyle 2 stroke:#d63031,stroke-width:3px
```

A per-block analysis would give the `if` block a single state, losing the branch refinement. Per-edge tracking stores `EdgeStates[{pred, succ}]` so each branch carries its own narrowing facts.

---

## NullState Lattice & Merge Semantics

How states combine at control-flow merge points.

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'fontSize': '14px'}}}%%
graph LR
    subgraph STATE["NullState Fields"]
        N["🛡️ NarrowedVars<br>NarrowedMembers<br>NarrowedThisMembers"]
        NB["⚠️ NullableVars<br>NullableThisMembers"]
    end

    subgraph MERGE["At Merge Point"]
        NI["∩ Intersection<br><i>keep only if ALL paths agree</i>"]
        NU["∪ Union<br><i>nullable if ANY path says so</i>"]
    end

    N --> NI
    NB --> NU

    classDef narrowed fill:#55efc4,stroke:#00b894,color:#2d3436,stroke-width:2px
    classDef nullable fill:#fdcb6e,stroke:#f39c12,color:#2d3436,stroke-width:2px
    classDef intersect fill:#74b9ff,stroke:#0984e3,color:#2d3436,stroke-width:2px
    classDef union fill:#ff7675,stroke:#d63031,color:#fff,stroke-width:2px

    class N narrowed
    class NB nullable
    class NI intersect
    class NU union

    style STATE fill:#dfe6e920,stroke:#b2bec3,stroke-width:2px,color:#636e72
    style MERGE fill:#6c5ce720,stroke:#6c5ce7,stroke-width:2px,color:#6c5ce7
```

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'fontSize': '14px'}}}%%
graph TB
    PATH_A["🅰️ Path A<br>NarrowedVars = {p, q}<br>NullableVars = {x}"]
    PATH_B["🅱️ Path B<br>NarrowedVars = {p}<br>NullableVars = {y}"]
    MERGED["🔗 Merged Result<br>NarrowedVars = {p}  ← ∩ intersection<br>NullableVars = {x, y}  ← ∪ union"]

    PATH_A -->|"merge"| MERGED
    PATH_B -->|"merge"| MERGED

    classDef pathA fill:#74b9ff,stroke:#0984e3,color:#2d3436,stroke-width:2px
    classDef pathB fill:#a29bfe,stroke:#6c5ce7,color:#fff,stroke-width:2px
    classDef merged fill:#55efc4,stroke:#00b894,color:#2d3436,stroke-width:3px

    class PATH_A pathA
    class PATH_B pathB
    class MERGED merged
```

---

## Transfer Functions

What happens when each statement type is visited.

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'fontSize': '13px'}}}%%
flowchart LR
    STMT["📄 Statement"] --> TYPE{"Statement<br>type?"}

    TYPE -->|DeclStmt| DECL["🏗️ Init check:<br>• _Nonnull / &x / new → narrow<br>• nullable source → mark nullable"]
    TYPE -->|"BinaryOp ="| ASSIGN["🔄 1. Invalidate old narrowing<br>2. Re-narrow if RHS is nonnull"]
    TYPE -->|"UnaryOp *"| DEREF["🔍 Check: is pointer nullable?<br>→ warn if yes"]
    TYPE -->|"MemberExpr ->"| ARROW["➡️ Check base pointer<br>Smart ptr op→ special case<br>this-> always suppressed"]
    TYPE -->|ArraySubscript| SUBSCRIPT["📦 Treat p[i] as *p<br>→ same deref check"]
    TYPE -->|CallExpr| CALL["📞 • __builtin_assume → narrow<br>• _Nonnull param → narrow arg<br>• sp.reset() → mark nullable<br>• std::move(sp) → mark nullable"]

    classDef input fill:#6c5ce7,stroke:#5a4bd1,color:#fff,stroke-width:2px
    classDef decision fill:#fdcb6e,stroke:#f39c12,color:#2d3436,stroke-width:2px
    classDef narrow fill:#55efc4,stroke:#00b894,color:#2d3436,stroke-width:2px
    classDef invalidate fill:#74b9ff,stroke:#0984e3,color:#2d3436,stroke-width:2px
    classDef warn fill:#ff7675,stroke:#d63031,color:#fff,stroke-width:2px
    classDef suppress fill:#dfe6e9,stroke:#b2bec3,color:#2d3436,stroke-width:2px
    classDef mixed fill:#a29bfe,stroke:#6c5ce7,color:#fff,stroke-width:2px

    class STMT input
    class TYPE decision
    class DECL narrow
    class ASSIGN invalidate
    class DEREF warn
    class ARROW suppress
    class SUBSCRIPT warn
    class CALL mixed
```

---

## Condition Analysis

How `analyzeCondition()` extracts narrowing facts from branch conditions.

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'fontSize': '14px'}}}%%
flowchart TD
    COND["🔍 Branch condition"] --> UNWRAP["unwrapBuiltinExpect()<br>Strip __builtin_expect /<br>LIKELY / UNLIKELY"]
    UNWRAP --> TERMINAL["getTerminalCondition()<br>Follow && / || RHS chain<br>to find leaf expression"]

    TERMINAL --> ANALYZE{"Condition<br>pattern?"}

    ANALYZE -->|"if (p)"| TRUTH["✅ Truthiness check<br>→ p narrowed on TRUE edge"]
    ANALYZE -->|"p != nullptr"| NEQ["✅ Not-equal-null<br>→ p narrowed on TRUE edge"]
    ANALYZE -->|"p == nullptr"| EQ["↩️ Equal-null<br>→ p narrowed on FALSE edge"]
    ANALYZE -->|"!p"| NEG["↩️ Negation<br>→ p narrowed on FALSE edge"]
    ANALYZE -->|"sp.operator bool()"| SPBOOL["✅ Smart pointer bool<br>→ sp narrowed on TRUE edge"]

    classDef input fill:#6c5ce7,stroke:#5a4bd1,color:#fff,stroke-width:2px
    classDef process fill:#74b9ff,stroke:#0984e3,color:#2d3436,stroke-width:2px
    classDef decision fill:#fdcb6e,stroke:#f39c12,color:#2d3436,stroke-width:2px
    classDef trueEdge fill:#55efc4,stroke:#00b894,color:#2d3436,stroke-width:2px
    classDef falseEdge fill:#fd79a8,stroke:#e84393,color:#fff,stroke-width:2px

    class COND input
    class UNWRAP,TERMINAL process
    class ANALYZE decision
    class TRUTH,NEQ,SPBOOL trueEdge
    class EQ,NEG falseEdge
```

---

## Gradual Adoption Decision Tree

How the per-function gating works (`SemaDecl.cpp` + `AnalysisBasedWarnings.cpp`).

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'fontSize': '14px'}}}%%
flowchart TD
    FUNC["📄 Function definition encountered"] --> CHECK_FLAG{"-fflow-sensitive-nullability<br>enabled?"}

    CHECK_FLAG -->|no| OFF["🚫 Analysis disabled<br>Zero overhead"]
    CHECK_FLAG -->|yes| CHECK_DEFAULT{"-fnullability-default=?"}

    CHECK_DEFAULT -->|"nullable or nonnull"| ENABLED["✅ FlowSensitiveNullabilityEnabled = true<br>Analysis runs on this function"]
    CHECK_DEFAULT -->|"unspecified (default)"| CHECK_PRAGMA{"Inside #pragma clang<br>assume_nonnull?"}

    CHECK_PRAGMA -->|yes| ENABLED
    CHECK_PRAGMA -->|no| OFF

    classDef start fill:#6c5ce7,stroke:#5a4bd1,color:#fff,stroke-width:2px
    classDef decision fill:#fdcb6e,stroke:#f39c12,color:#2d3436,stroke-width:2px
    classDef enabled fill:#55efc4,stroke:#00b894,color:#2d3436,stroke-width:3px
    classDef disabled fill:#ff7675,stroke:#d63031,color:#fff,stroke-width:3px

    class FUNC start
    class CHECK_FLAG,CHECK_DEFAULT,CHECK_PRAGMA decision
    class ENABLED enabled
    class OFF disabled
```

---

## File Map

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'fontSize': '13px'}}}%%
graph LR
    subgraph L1["🧠 Layer 1 — Analysis Engine"]
        H["FlowNullability.h<br><i>Handler interface + entry point</i>"]
        CPP["FlowNullability.cpp<br><i>~1010 lines: worklist, transfer fns,<br>condition analysis, edge states</i>"]
    end

    subgraph L2["🔌 Layer 2 — Sema Glue"]
        ABW["AnalysisBasedWarnings.cpp<br><i>FlowNullabilityReporter,<br>shouldEnable, CFG build</i>"]
        SEMA_H["Sema.h<br><i>FlowSensitiveNullabilityEnabled flag</i>"]
        SEMA_CPP["Sema.cpp<br><i>Helper functions</i>"]
    end

    subgraph L3["🚦 Layer 3 — Gating"]
        SDECL["SemaDecl.cpp<br><i>Per-function enable in<br>ActOnStartOfFunctionDef</i>"]
    end

    subgraph INFRA["⚙️ Flags & Diagnostics"]
        OPTS["Options.td<br><i>Flag definitions</i>"]
        DRIVER["Clang.cpp<br><i>Driver forwarding</i>"]
        DIAG_KINDS["DiagnosticSemaKinds.td<br><i>Warning message</i>"]
        DIAG_GROUPS["DiagnosticGroups.td<br><i>-Wflow-nullable-dereference</i>"]
    end

    subgraph TESTS_GRP["🧪 Tests"]
        TESTS["test/Sema/flow-nullability-*.cpp/.c<br><i>28+ lit tests</i>"]
    end

    H --> CPP
    CPP --> ABW
    ABW --> SDECL
    ABW --> DIAG_KINDS
    SEMA_H --> ABW
    SEMA_H --> SDECL
    OPTS --> DRIVER

    classDef engine fill:#55efc4,stroke:#00b894,color:#2d3436,stroke-width:2px
    classDef glue fill:#74b9ff,stroke:#0984e3,color:#2d3436,stroke-width:2px
    classDef gate fill:#fdcb6e,stroke:#f39c12,color:#2d3436,stroke-width:2px
    classDef infra fill:#dfe6e9,stroke:#b2bec3,color:#2d3436,stroke-width:2px
    classDef test fill:#a29bfe,stroke:#6c5ce7,color:#fff,stroke-width:2px

    class H,CPP engine
    class ABW,SEMA_H,SEMA_CPP glue
    class SDECL gate
    class OPTS,DRIVER,DIAG_KINDS,DIAG_GROUPS infra
    class TESTS test

    style L1 fill:#55efc420,stroke:#00b894,stroke-width:2px,color:#00b894
    style L2 fill:#74b9ff20,stroke:#0984e3,stroke-width:2px,color:#0984e3
    style L3 fill:#fdcb6e20,stroke:#f39c12,stroke-width:2px,color:#f39c12
    style INFRA fill:#dfe6e920,stroke:#b2bec3,stroke-width:2px,color:#636e72
    style TESTS_GRP fill:#a29bfe20,stroke:#6c5ce7,stroke-width:2px,color:#6c5ce7
```

---

## Comparison with Sibling Analyses

This analysis follows the same pattern as two other Clang analyses:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'fontSize': '14px'}}}%%
graph TB
    subgraph PATTERN["📐 Common Pattern"]
        A_LIB["lib/Analysis/*.cpp<br>Standalone analysis engine"]
        A_HANDLER["Handler callback interface"]
        A_ABW["AnalysisBasedWarnings.cpp<br>Reporter + wiring"]
    end

    subgraph TS_GRP["🔒 ThreadSafety"]
        TS["ThreadSafety.cpp"]
        TS_H["ThreadSafetyHandler"]
        TS_R["ThreadSafetyReporter"]
    end

    subgraph UV_GRP["❓ UninitializedValues"]
        UV["UninitializedValues.cpp"]
        UV_H["UninitHandler"]
        UV_R["UninitValsDiagReporter"]
    end

    subgraph FN_GRP["🛡️ FlowNullability"]
        FN["FlowNullability.cpp"]
        FN_H["FlowNullabilityHandler"]
        FN_R["FlowNullabilityReporter"]
    end

    A_LIB -.->|"pattern"| TS
    A_LIB -.->|"pattern"| UV
    A_LIB -.->|"pattern"| FN

    A_HANDLER -.-> TS_H
    A_HANDLER -.-> UV_H
    A_HANDLER -.-> FN_H

    A_ABW -.-> TS_R
    A_ABW -.-> UV_R
    A_ABW -.-> FN_R

    classDef pattern fill:#dfe6e9,stroke:#b2bec3,color:#2d3436,stroke-width:2px
    classDef thread fill:#fdcb6e,stroke:#f39c12,color:#2d3436,stroke-width:2px
    classDef uninit fill:#74b9ff,stroke:#0984e3,color:#2d3436,stroke-width:2px
    classDef flow fill:#55efc4,stroke:#00b894,color:#2d3436,stroke-width:2px

    class A_LIB,A_HANDLER,A_ABW pattern
    class TS,TS_H,TS_R thread
    class UV,UV_H,UV_R uninit
    class FN,FN_H,FN_R flow

    style PATTERN fill:#dfe6e920,stroke:#b2bec3,stroke-width:2px,color:#636e72
    style TS_GRP fill:#fdcb6e20,stroke:#f39c12,stroke-width:2px,color:#f39c12
    style UV_GRP fill:#74b9ff20,stroke:#0984e3,stroke-width:2px,color:#0984e3
    style FN_GRP fill:#55efc420,stroke:#00b894,stroke-width:2px,color:#00b894
```

**Key difference**: ThreadSafety and UninitializedValues use **per-block** state. FlowNullability uses **per-edge** state for more precise branch refinement — a technique from the dataflow analysis literature known as "edge-based dataflow" (similar to conditional constant propagation).
