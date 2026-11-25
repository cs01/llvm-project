#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(int c,char**v){int a[2]={0};int n=2;int*__counted_by(n)p=a;if(__builtin_add_overflow(c,20,&n))return 1;return p[12];}
