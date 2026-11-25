#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
NI int f(const int*__terminated_by(5) p){int n=0;while(*p!=5){p++;n++;}return n;}typedef int(*T)(const int*__null_terminated);int main(void){int x[2]={3,0};T v=f;return v(x);}
