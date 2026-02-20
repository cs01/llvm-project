// AND pattern narrows both operands
void process(int* p, int* q) {
    if (p && q) {
        *p = *q;  // OK - both non-null
    }
}

void either_or(int* p, int* q) {
    if (p || q) {
        *p = 42;  // warning - p might still be null!
    }
}