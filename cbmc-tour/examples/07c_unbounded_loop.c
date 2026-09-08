int twice(int x)
  __CPROVER_requires(x >= 0 && x < 1000)
  __CPROVER_ensures(__CPROVER_return_value == 2 * x)
{ int r=0; for(int i=0;i<x;i++) r+=2; return r; }  // unbounded loop

int main(void) {
  int y = nondet_int();
  __CPROVER_assume(y >= 0 && y < 1000);
  int r = twice(y);
  __CPROVER_assert(r >= y, "result at least input");
  return 0;
}
