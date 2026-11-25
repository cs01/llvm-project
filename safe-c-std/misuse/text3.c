#include "text.h"
static struct str mk(void){ char b[4]={1,2,3,4}; struct str s={b,4}; return s; } int main(void){ struct str s=mk(); return s.p[0]; }
