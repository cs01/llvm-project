#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ char a[2]; volatile size_t n=20; wchar_t w[9]={L'a',L'b',L'c',L'd'}; return (int)wcstombs(a,L"abcdefghijklmnop",n); }
