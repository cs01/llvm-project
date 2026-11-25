struct h { int n; const int *_Static __counted_by(n) f; };
static const int A[1], B[3];
int main(void) {
  struct h x = {1, A}, y = {1, A};
  struct h *p = &x;
  p->n = 3;
  p = &y;
  p->f = B;
  return x.f[2];
}
