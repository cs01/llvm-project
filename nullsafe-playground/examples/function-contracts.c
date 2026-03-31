// _Nonnull and _Nullable let you express contracts
// in function signatures. The compiler enforces them.

// _Nullable documents "this can be NULL"
int* _Nullable find_item(int key);

void use_result(void) {
    int* item = find_item(42);
    *item = 0;  // warning: find_item may return NULL

    if (item) {
        *item = 0;  // OK — checked
    }
}

// _Nonnull promises the pointer is never NULL
void process(int* _Nonnull data) {
    *data = 42;  // OK — guaranteed non-null by caller
}

// Passing nullable to _Nonnull — the compiler catches it
void caller(int* _Nullable x) {
    process(x);  // warning: passing nullable to nonnull param

    if (x) {
        process(x);  // OK — x was checked
    }
}

// Returning nullable from a _Nonnull function
int* _Nonnull bad_lookup(int* _Nullable p) {
    return p;  // warning: returning nullable from nonnull function
}

int* _Nonnull good_lookup(int* _Nullable p, int* _Nonnull fallback) {
    if (p)
        return p;        // OK — p was checked
    return fallback;     // OK — fallback is _Nonnull
}
