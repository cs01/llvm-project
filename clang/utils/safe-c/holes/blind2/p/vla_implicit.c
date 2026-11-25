// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ int n=5000*c; int a[4]={0}; int(*p)[n]=&a; for(int i=0;i<n;i++)(*p)[i]=1; printf("%d\n",a[0]); }
