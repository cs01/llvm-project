// Where the Clang Static Analyzer wins: a null check laundered through a bool.
// CSA's path-sensitivity proves this is SAFE; our flow analysis does not
// correlate `ok` with `p != 0`, so it emits a (rare) false positive here.
//
// Run nullsafe fork (WARNS — false positive):
//   ../build/bin/clang -fsyntax-only -fflow-sensitive-nullability \
//       -fnullability-default=nullable csa-wins-correlated.c
//
// Run Clang Static Analyzer (SILENT — correct):
//   ../build/bin/clang --analyze -Xclang -analyzer-output=text csa-wins-correlated.c

int * _Nullable get(void);

int laundered_guard(void) {
    int *p = get();
    int ok = (p != 0);           // check stored in a separate variable
    if (ok) return *p;           // SAFE: ok implies p != 0 — CSA sees this
    return 0;
}

// Contrast: a DIRECT guard is understood by the fork — no false positive.
int direct_guard(void) {
    int *p = get();
    if (p) return *p;            // OK in both tools
    return 0;
}
