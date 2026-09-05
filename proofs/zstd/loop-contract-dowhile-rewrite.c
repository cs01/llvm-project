unsigned int nondet_uint(void);
void fill(char *buf, unsigned len)
{
    unsigned i = 0;
    // do { B } while (C)  ==>  while (1) { B; if (!C) break; }
    while (1)
    __CPROVER_assigns(i, __CPROVER_object_upto(buf, len))
    __CPROVER_loop_invariant(i < len)
    __CPROVER_decreases(len - i)
    {
        buf[i] = 7;
        i++;
        if (!(i < len)) break;
    }
}
void harness(void)
{
    unsigned len = nondet_uint();
    __CPROVER_assume(len > 0 && len <= 100000);
    char *buf = __CPROVER_allocate(len, 0);
    fill(buf, len);
}
