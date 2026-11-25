#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ wchar_t w[2]; volatile size_t n=20; return (int)mbstowcs(w,"aaaaaaaaaaaaaaaaaaaaaaaa",n); }
