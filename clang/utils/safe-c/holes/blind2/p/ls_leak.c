// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>
int main(void){ struct { wchar_t s[3]; wchar_t sec[4]; } x={{'1','2','3'},{'S','E','C',0}}; printf("%ls\n",x.s);}
