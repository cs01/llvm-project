// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ char*q=NULL; { char a[c+10]; a[0]=1; q=__builtin_alloca(16); for(int i=0;i<16;i++) q[i]=3; printf("%d",a[0]); } char b[c+100]; memset(b,7,sizeof b); printf("%d %d\n",q?q[0]:9,b[2]); }
