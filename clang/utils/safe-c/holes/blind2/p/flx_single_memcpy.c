// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct f{int n; int d[__counted_by(n)];};
static int g(struct f*p,const char*__counted_by(4) b){ memcpy(p,b,4); return p->d[50]; }
int main(void){ struct f*p=malloc(sizeof*p+8); if(!p) return 1; p->n=2; char b[4]={100,0,0,0}; printf("%d\n",g(p,b)); free(p); }
