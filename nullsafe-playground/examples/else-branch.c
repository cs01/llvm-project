// Else branch knows condition was false
void example(int* data) {
    if (data) {
        *data = 42;  // OK
    } else {
        *data = 0;  // warning - data is null here!
    }
}

void inverted(int* data) {
    if (!data) {
        return;
    }
    *data = 42;  // OK - data is non-null
}