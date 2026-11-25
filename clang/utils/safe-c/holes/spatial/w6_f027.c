#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
NI const char*__terminated_by('/') f(void){static const char a[]={'a','/'};return a;}typedef const char*(*T)(void);int main(void){T t=f;int n=0;const char*p=t();while(*p){p++;n++;}return n;}
