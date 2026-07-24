// Templates: the fork checks the template DEFINITION against its annotations,
// so a nullable-deref contract violation is caught once, regardless of how the
// template is later instantiated. CSA only reasons about concrete instantiations
// and feasible paths, so with a non-null argument it sees no bug to report.
//
// Run nullsafe fork (WARNS at the template body):
//   ../build/bin/clang -fsyntax-only -std=c++17 -fflow-sensitive-nullability \
//       -fnullability-default=nullable template-contract.cpp
//
// Run Clang Static Analyzer (SILENT here — the callsite passes a non-null arg):
//   ../build/bin/clang --analyze -std=c++17 -Xclang -analyzer-output=text template-contract.cpp

template <class T>
int deref(T* _Nullable p) {
    return *p;                   // warning: unchecked deref of nullable param
}

template <class T>
int deref_safe(T* _Nullable p) {
    if (!p) return 0;
    return *p;                   // OK — guarded
}

int use(void) {
    int x = 5;
    int *q = &x;                 // non-null concrete arg
    return deref(q) + deref_safe(q);
}
