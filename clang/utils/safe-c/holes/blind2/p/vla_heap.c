// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ int n=5000*c; int(*p)[n]=malloc(16); if(!p) return 1; for(int i=0;i<n;i++)(*p)[i]=1; printf("%d\n",(*p)[0]); }
