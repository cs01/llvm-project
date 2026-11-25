#include <stdio.h>
#include <stdlib.h>
static int cmp(const void*a,const void*b){ const int*__single x,*__single y; __unsafe { x=a; y=b; } return (*x>*y)-(*x<*y); }
int main(void){ int a[3]={3,1,2}; qsort(a,3,sizeof a[0],cmp); printf("%d\n",a[0]); }
