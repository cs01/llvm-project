// Early returns narrow type for rest of function
void process(char* str) {
    if (!str) return;  // guard clause

    // str is now proven non-null
    *str = 'x';  // OK
}

void multi_guard(char* p, char* q) {
    if (!p || !q) return;

    // Both p and q are non-null here
    *p = *q;  // OK
}