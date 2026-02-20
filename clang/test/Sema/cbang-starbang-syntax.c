// RUN: %clang_cc1 -fsyntax-only -fblocks %s -verify

typedef int*! nonnull_int_ptr;
typedef int* nullable_int_ptr;

void takes_nonnull(int*! p) {
    *p = 42;
}

void takes_nullable(int* p) {
    if (p) {
        *p = 42;
        *p = 23;
    } else {
        *p=7;  // expected-error{{dereferencing nullable pointer of type 'int * _Nullable'}}
    }
}

int*! returns_nonnull(void) {
    static int x = 100;
    return &x;
}

int* returns_nullable(void) {
    return 0;
}

void test_basic_syntax(void) {
    int value = 10;
    int*! nonnull_ptr = &value;
    int* nullable_ptr = &value;
    takes_nonnull(&value);
    takes_nullable(&value);
}

void test_null_to_nonnull(void) {
    takes_nonnull(0); // expected-error{{null passed to a callee that requires a non-null argument}}
}

void test_nullable_to_nonnull(void) {
    int* nullable = returns_nullable();
    takes_nonnull(nullable); // expected-error{{implicit conversion from nullable pointer 'int * _Nullable' to non-nullable pointer type 'int * _Nonnull'}}
}

void test_nonnull_to_nullable(void) {
    int*! nonnull = returns_nonnull();
    takes_nullable(nonnull);
}

void test_assignment(void) {
    int value = 42;
    int*! nonnull;
    int* nullable = 0;

    nonnull = &value;
    nonnull = nullable;   // expected-error{{implicit conversion from nullable pointer 'int * _Nullable' to non-nullable pointer type 'int * _Nonnull'}}
    nullable = nonnull;
}

void test_function_pointers(void) {
    void (*_Nonnull fp1)(int*!) = takes_nonnull;
    void (*fp2)(int*!) = takes_nonnull;
}

void test_typedef(void) {
    nonnull_int_ptr p1;
    nullable_int_ptr p2;

    int x = 42;
    p1 = &x;
    p2 = &x;
    p2 = 0;

    p1 = p2;  // expected-error{{implicit conversion from nullable pointer 'nullable_int_ptr _Nullable' (aka 'int *') to non-nullable pointer type 'nonnull_int_ptr' (aka 'int *')}}
}

void test_return_types(void) {
    int*! nonnull = returns_nullable();  // expected-error{{implicit conversion from nullable pointer 'int * _Nullable' to non-nullable pointer type 'int * _Nonnull'}}
    int* nullable = returns_nonnull();
}

void test_defaults(int* implicitly_nullable, int*! explicitly_nonnull) {
    takes_nullable(implicitly_nullable);
    takes_nonnull(implicitly_nullable);    // expected-error{{implicit conversion from nullable pointer 'int * _Nullable' to non-nullable pointer type 'int * _Nonnull'}}
    takes_nullable(explicitly_nonnull);
    takes_nonnull(explicitly_nonnull);
}

void test_multi_level(int** nullable_ptr_to_nullable,
                     int**! nonnull_ptr_to_nullable) {
}

void test_flow_narrowing_basic(int* p) {
    if (p) {
        takes_nonnull(p);
    }
}

void test_flow_narrowing_explicit(int* p) {
    if (p != 0) {
        takes_nonnull(p);
    }
}

void test_flow_no_check(int* p) {
    takes_nonnull(p);  // expected-error{{implicit conversion from nullable pointer 'int * _Nullable' to non-nullable pointer type 'int * _Nonnull'}}
}

void test_flow_after_if(int* p) {
    if (p) {
        takes_nonnull(p);
    }
    takes_nonnull(p);  // expected-error{{implicit conversion from nullable pointer 'int * _Nullable' to non-nullable pointer type 'int * _Nonnull'}}
}

void test_flow_else(int* p) {
    if (p) {
        takes_nonnull(p);
    } else {
        takes_nonnull(p);  // expected-error{{implicit conversion from nullable pointer 'int * _Nullable' to non-nullable pointer type 'int * _Nonnull'}}
    }
}

void test_flow_dereference(int* p) {
    if (p) {
        *p = 42;
    }
}

void test_flow_dereference_no_check(int* p) {
    *p = 42;  // expected-error{{dereferencing nullable pointer of type 'int * _Nullable'}}
}

void test_early_return_simple(char* p) {
    if (p == 0) return;
    *p = 'x';
}

void test_early_return_negated(char* p) {
    if (!p) return;
    *p = 'x';
}

void test_early_return_compound(char* p, char* q) {
    if (!p || !q) return;
    *p = 'x';
    *q = 'y';
}

void test_early_return_explicit_or(char* p, char* q) {
    if ((p == 0) || (q == 0)) {
        return;
    }
    *p = 'x';
    *q = 'y';
}

void test_early_return_braces(char* p) {
    if (p == 0) {
        return;
    }
    *p = 'x';
}

int some_condition(void);
void test_and_pattern(char* p) {
    if (p && some_condition()) {
        *p = 'x';
    }
}

void test_and_deref_simple(char* p) {
    if (p && *p == 'x') {
        *p = 'y';
    }
}

void test_and_deref_chained(char* p, char* q) {
    if (p && q && *p == *q) {
        *p = 'a';
        *q = 'b';
    }
}

int check_char(char c);
void test_and_deref_funcall(char* p) {
    if (p && check_char(*p)) {
        *p = 'x';
    }
}

void test_null_assignment(void) {
    int* nullable = 0;
    int*! nonnull = 0;   // expected-error{{null passed to a callee that requires a non-null argument}}
}

void test_while_loop(int* p) {
    while (p) {
        *p = 42;
        p = 0;
    }
    *p = 0;  // expected-error{{dereferencing nullable pointer of type 'int * _Nullable'}}
}

void test_for_loop(int* p) {
    for (; p; p = 0) {
        *p = 42;
    }
}

void test_else_narrowing(int* p) {
    if (!p) {
        *p = 42;  // expected-error{{dereferencing nullable pointer of type 'int * _Nullable'}}
    } else {
        *p = 42;
    }
}

void test_multiple_and(int* p, int* q, int* r) {
    if (p && q && r) {
        *p = 1;
        *q = 2;
        *r = 3;
    }
}

void test_ternary(int* nullable, int*! nonnull, int cond) {
    int* result1 = cond ? nullable : nonnull;
    int*! result2 = cond ? nonnull : nullable; // expected-error{{implicit conversion from nullable pointer 'int * _Nullable' to non-nullable pointer type 'int * _Nonnull'}}
    int*! result3 = cond ? nonnull : nonnull;
}

void process_int(int val);
void test_deref_in_call(int* p) {
    if (p) {
        process_int(*p);
    }
    process_int(*p);  // expected-error{{dereferencing nullable pointer of type 'int * _Nullable'}}
}

void test_array_subscript(int* arr) {
    if (arr) {
        int x = arr[0];
    }
    int y = arr[0];
}

void test_pointer_arithmetic(int* p) {
    if (p) {
        int* q = p + 1;
        *q = 42;
    }
}

void test_address_of(void) {
    int x = 42;
    int*! p = &x;
    takes_nonnull(&x);
}

struct Point { int x; int y; };
void test_struct_deref(struct Point* p) {
    if (p) {
        p->x = 10;
    }
    p->y = 20;
}

void test_chained_deref(int** pp) {
    if (pp) {
        *pp = 0;
    }

    if (pp && *pp) {
        **pp = 42;
    }
}

void test_triple_pointers(int*** ppp) {
    if (ppp && *ppp && **ppp) {
        ***ppp = 42;
    }
}

void test_nonnull_outer_ptr(int**! pp) {
    *pp = 0;

    if (*pp) {
        **pp = 42;
    }
}

void test_early_return_unreachable(int* p) {
    if (!p) {
        *p = 42;  // expected-error{{dereferencing nullable pointer of type 'int * _Nullable'}}
        return;
    }
    *p = 0;
}

void test_and_or_mixed(int* p, int cond) {
    if (p || cond) {
        *p = 42;  // expected-error{{dereferencing nullable pointer of type 'int * _Nullable'}}
    }
}

void test_comparison_narrowing(int* p, int* q) {
    if (p == q && p) {
        *p = 42;
        *q = 42;  // expected-error{{dereferencing nullable pointer of type 'int * _Nullable'}}
    }
}
