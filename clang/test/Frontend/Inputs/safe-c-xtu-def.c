__attribute__((annotate("safec_unsynchronized"))) int tbl[1];
__attribute__((annotate("safec_unsynchronized"))) long shared;
__attribute__((annotate("safec_unsynchronized"))) int ok_tbl[4];
int add(int a, int b) { return a + b; }
