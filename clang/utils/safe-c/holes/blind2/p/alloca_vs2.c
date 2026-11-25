// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static char*_Nullable z;
int main(int c,char**v){ char*q=NULL; for(int i=0;i<2;i++){ char a[c*10+i*64]; a[0]=1; char*t=__builtin_alloca(16); memset(t,i+3,16); if(!q) q=t; printf("%d %d\n",a[0],q[0]); } printf("%d\n",q?q[0]:9); }
