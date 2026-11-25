#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ const char *__null_terminated _Nullable p=setlocale(LC_ALL,"C"); for(int i=0;i<3;i++){ if(p&&strlen(p)>99) return 2; } return 0; }
