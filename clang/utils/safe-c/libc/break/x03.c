#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
int main(void){ char a[1]; volatile size_t n=8; return mblen(a,n); }
