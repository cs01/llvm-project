int main(void) {
  int a = nondet_int();
  int b = a + 1;
  if (b > a)
    b = b - a;
  __CPROVER_assert(b == 1, "b is one");
  return 0;
}
