// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static int cmp(const void*a,const void*b){ const int*x=a,*y=b; return (*x>*y)-(*x<*y); }
int main(void){ int a[3]={3,1,2}; qsort(a,3,sizeof a[0],cmp); printf("%d\n",a[0]); }
