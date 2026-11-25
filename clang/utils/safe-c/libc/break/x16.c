#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ FILE *_Owned _Nullable f=tmpfile(); if(!f) return 1; fclose(f); fpos_t p; return fgetpos(f,&p); }
