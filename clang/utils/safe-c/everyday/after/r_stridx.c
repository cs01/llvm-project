#include <stdio.h>
static int count(const char*s){ int n=0; for(;*s;s++) if(*s==' ') n++; return n; }
int main(void){ printf("%d\n",count("a b")); }
