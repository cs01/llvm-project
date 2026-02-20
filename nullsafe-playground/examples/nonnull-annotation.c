void process(int* _Nonnull data) {
    *data = 42;
}

void example(int* x, int* _Nonnull y) {
    process(x);  // error
    process(y);  // OK
}
