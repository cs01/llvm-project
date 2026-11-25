#include <ptrcheck.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(int c,char**v){int n=5000*c;int a[4]={0};int(*p)[n]=(int(*)[n])&a;for(int i=0;i<n;i++)(*p)[i]=1;return a[0];}
