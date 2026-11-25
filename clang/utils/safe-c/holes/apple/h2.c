#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void f(int*__counted_by(n) p,int n){p[n-1]=7;}
int main(void){void(*g)(int*,int)=f;int*h=malloc(4);if(!h)return 1;g(h,9);return 0;}
