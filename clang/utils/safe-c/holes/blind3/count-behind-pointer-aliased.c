static void f(int *__counted_by(*n) p, int *n, int *m) { *m = 5; p[4] = 1; }
int main(void) { int a[4] = {0}, k = 4; f(a, &k, &k); return 0; }
