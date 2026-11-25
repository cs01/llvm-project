int main(void) {
  int a[2] = {0};
  int m = 2;
  int *q = &m;
  int *__counted_by(m) p = a;
  *q = 3;
  p[2] = 1;
  return 0;
}
