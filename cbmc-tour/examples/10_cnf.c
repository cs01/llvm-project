int main(void) {
  unsigned char a = nondet_uchar();   // just 8 bits
  unsigned char b = nondet_uchar();
  __CPROVER_assert((unsigned char)(a + b) != 42, "sum is never 42");
  return 0;
}
