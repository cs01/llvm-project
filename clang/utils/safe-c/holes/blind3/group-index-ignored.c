struct h { int n; const int *_Static __counted_by(n) f; };
static const int A[1], B[3];
int main(void) {
  struct h x[2] = {{1, A}, {1, A}};
  x[0].n = 3;
  x[1].f = B;
  return x[0].f[2];
}
