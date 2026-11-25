#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ wchar_t a[2],b[2]={0}; volatile size_t n=9; wmemcpy(a,b,n); return 0; }
