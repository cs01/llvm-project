#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ wchar_t w[2]; mbstowcs(w,"aaaaaa",2); return (int)wcslen(w); }
