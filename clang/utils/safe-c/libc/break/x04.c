#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ char *_Owned _Nullable a=malloc(4); if(!a) return 1; free(a); wchar_t w; return mbtowc(&w,a,4); }
