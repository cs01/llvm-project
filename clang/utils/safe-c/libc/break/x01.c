#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ char b[8]="1.5"; char *__null_terminated e=NULL; strtold(b,&e); memset(b,'x',8); return e?(int)strlen(e):0; }
