// Standard Clang compiles this with ZERO warnings.
// Nullability Safety catches the bug at compile time.

int deref(int *p) {
    return *p;  // BUG: crashes if p is NULL
}
