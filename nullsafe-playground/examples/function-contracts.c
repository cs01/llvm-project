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

// Without annotations, the compiler infers nullability
// from how you use the pointer
void caller(int* x) {
    *x = 1;     // warning: x might be NULL

    if (x) {
        *x = 1;  // OK — x was checked
    }
}
