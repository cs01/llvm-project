#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ const wchar_t *_Nullable p=NULL; { wchar_t a[2],b[2]={0}; p=wmemcpy(a,b,2); } return p?(int)p[1]:0; }
