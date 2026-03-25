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
        // p is known NULL here — any use would warn
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

// Assertion macros work too — any [[noreturn]] function
extern _Noreturn void abort(void);
#define ASSERT(x) do { if (!(x)) abort(); } while(0)

void with_assert(int* p) {
    ASSERT(p);
    *p = 42;  // OK — ASSERT proved p is non-null
}
