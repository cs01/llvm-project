#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
NI int f(const char*__terminated_by('/')p){int n=0;while(*p!='/'){p++;n++;}return n;}typedef int(*T)(const char*);int main(void){char b[2]="a";T v=f;return v(b);}
