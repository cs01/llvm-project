int main(void) {
  int s = 0;
  for (int i = 1; i <= 5; i++) s += i * i;
  __CPROVER_output("sum_of_squares", s);
  return 0;
}
