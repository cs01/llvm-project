// Standard Clang compiles this with ZERO warnings.
// Nullsafe Clang catches the bug at compile time.

int deref(int *p) {
    return *p;  // BUG: crashes if p is NULL
}
