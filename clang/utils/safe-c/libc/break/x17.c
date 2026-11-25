#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
static void g(void*p){ } int main(void){ char b[8]="abcdefg"; char *__null_terminated _Nullable x=setlocale(LC_ALL,NULL); if(x) *x='a'; return 0; }
