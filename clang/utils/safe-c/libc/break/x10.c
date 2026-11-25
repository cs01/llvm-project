#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ wchar_t w[2]={0}; char a[8]; w[0]=L'a'; w[1]=L'b'; return (int)wcstombs(a,w,8); }
