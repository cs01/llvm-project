#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ const char *__null_terminated _Nullable p=setlocale(LC_ALL,"C"); setlocale(LC_ALL,"en_US.UTF-8"); return p?(int)strlen(p):0; }
