void deref_twice_unsafe(int**  pp) {
    **pp = 42;  // Error
}

void deref_twice_safe(int**  pp) {
    if (pp && *pp) {
        **pp = 42;  // OK
    }
}

void deref_twice_partial(int**  pp) {
    if (pp) {
        **pp = 42;  // Error
    }
}

void example_nonnull_inner(int * _Nonnull * _Nonnull pp) {
    **pp = 42;  // OK
}