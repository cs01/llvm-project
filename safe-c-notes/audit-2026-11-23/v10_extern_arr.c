#include <stdio.h>
extern int tbl[];
extern int tbl2[1000];
int main(int argc, char **argv) { tbl[argc * 100000] = 1; tbl2[999] = 2; return 0; }
