// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable %s -verify

// Test that function calls do NOT invalidate narrowing.
// Functions receive a copy of pointer arguments, so they cannot modify
// the original pointer variable to make it null.

void takes_int(int x);
void takes_ptr(int *p);

void test_narrowing_preserved_after_call(int *p) {
    if (p) {
        // p is narrowed to nonnull
        takes_int(42);  // Function call doesn't invalidate p's narrowing
        *p = 1;  // OK - p is still nonnull
    }
}

void test_narrowing_preserved_pass_ptr(int *p) {
    if (p) {
        // p is narrowed to nonnull
        takes_ptr(p);  // Even passing p doesn't invalidate (pass by value)
        *p = 1;  // OK - p is still nonnull
    }
}

void test_multiple_calls(int *p, int *q) {
    if (p && q) {
        // Both p and q narrowed to nonnull
        takes_ptr(p);
        takes_ptr(q);
        takes_int(42);
        *p = 1;  // OK - narrowing preserved through all calls
        *q = 2;  // OK - narrowing preserved through all calls
    }
}

// expected-no-diagnostics
