// Tests that passing a pointer to a _Nonnull parameter narrows it as non-null.
// Also tests GCC-style __attribute__((nonnull)) which is used by glibc/bionic.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

typedef unsigned long size_t;

// Simulate system header declarations with _Nonnull params
size_t my_strlen(const char * _Nonnull s);
void my_use(const char * _Nonnull s);
void unannotated_use(const char *s);
void two_params(const char * _Nonnull a, const char *b);

// GCC-style nonnull attribute (used by glibc strlen, memcpy, etc.)
size_t gcc_strlen(const char *s) __attribute__((nonnull(1)));
void gcc_all_nonnull(const char *a, const char *b) __attribute__((nonnull));
void gcc_partial_nonnull(const char *a, const char *b) __attribute__((nonnull(1)));

#pragma clang assume_nonnull begin

// After passing to a _Nonnull param, the pointer is narrowed — no warning.
void test_nonnull_param_narrows(const char * _Nullable filePath) {
    my_strlen(filePath); // expected-warning{{implicit conversion from nullable}}
    const char c = *filePath; // OK — narrowed by call above
}

// Passing to an unannotated param does NOT narrow (unannotated_use's param
// has no annotation, so inside assume_nonnull it becomes implicitly nonnull —
// but that's an implicit annotation, not an explicit _Nonnull).
void test_unannotated_param_no_narrow(const char * _Nullable filePath) {
    unannotated_use(filePath);
    const char c = *filePath; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// Multiple args: only _Nonnull ones narrow.
void test_mixed_params(const char * _Nullable a, const char * _Nullable b) {
    two_params(a, b); // expected-warning{{implicit conversion from nullable}}
    const char c1 = *a; // OK — narrowed
    const char c2 = *b; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// Multiple _Nonnull calls in sequence.
void test_multiple_calls(const char * _Nullable p, const char * _Nullable q) {
    my_use(p); // expected-warning{{implicit conversion from nullable}}
    my_strlen(q); // expected-warning{{implicit conversion from nullable}}
    const char c1 = *p; // OK
    const char c2 = *q; // OK
}

// --- GCC-style __attribute__((nonnull)) ---

// nonnull(1) narrows the first arg, like glibc's strlen.
void test_gcc_nonnull_attr(const char * _Nullable filePath) {
    gcc_strlen(filePath);
    const char c = *filePath; // OK — narrowed by gcc nonnull attr
}

// nonnull (no args) means ALL pointer params are nonnull.
void test_gcc_nonnull_all(const char * _Nullable a, const char * _Nullable b) {
    gcc_all_nonnull(a, b);
    const char c1 = *a; // OK — narrowed
    const char c2 = *b; // OK — narrowed
}

// nonnull(1) only narrows the first arg, not the second.
void test_gcc_nonnull_partial(const char * _Nullable a, const char * _Nullable b) {
    gcc_partial_nonnull(a, b);
    const char c1 = *a; // OK — narrowed (param 1 is nonnull)
    const char c2 = *b; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

#pragma clang assume_nonnull end
