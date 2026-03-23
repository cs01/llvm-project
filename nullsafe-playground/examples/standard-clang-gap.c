// Standard Clang has _Nullable and _Nonnull, but it only
// checks them at assignment boundaries. It does NOT track
// null checks through control flow.
//
// This means standard Clang:
//   1. Misses bugs where unchecked pointers are dereferenced
//   2. Cannot give you credit for null checks you already wrote
//
// Nullsafe C fills this gap with flow-sensitive analysis.

void the_gap(int* _Nullable p) {
    // Standard clang: no warning (it doesn't track dereferences)
    // Nullsafe C: warning — p is nullable!
    *p = 42;
}

void already_checked(int* _Nullable p) {
    if (!p) return;

    // Standard clang: still considers p _Nullable (no flow tracking)
    // Nullsafe C: knows p is non-null — no warning
    *p = 42;
}

// The classic mistake: check one pointer, use another
void wrong_check(int* _Nullable p, int* _Nullable q) {
    if (p) {
        *q = 0;  // Bug! Checked p but used q. Nullsafe C catches this.
    }
}
