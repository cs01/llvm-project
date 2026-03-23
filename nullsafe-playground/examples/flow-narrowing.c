// The compiler tracks null checks through control flow.
// After you check a pointer, it knows it's safe — no
// redundant warnings, no annotations needed.

void guard_clause(int* data) {
    if (!data) return;

    // data is proven non-null from here on
    *data = 42;  // OK — no warning
}

void if_else(int* p) {
    if (p) {
        *p = 1;  // OK — p checked
    } else {
        *p = 0;  // warning — p is null here!
    }
}

void and_pattern(int* p, int* q) {
    if (p && q) {
        *p = *q;  // OK — both checked
    }
}

void ternary(int* p) {
    int val = p ? *p : 0;  // OK — guarded by ternary
}
