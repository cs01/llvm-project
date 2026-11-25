#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void){_Atomic int*p=calloc(2,4);if(!p)return 1;for(int i=0;i<9;i++)p[i]++;return 0;}
