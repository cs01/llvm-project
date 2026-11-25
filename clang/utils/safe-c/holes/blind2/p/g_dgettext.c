// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
__attribute__((format_arg(1))) static const char*tr(const char*s){ return s; }
int main(void){ struct{char b[2]; char s[8];}x={{'a','b'},"SEC"}; printf(tr("%s\n"),x.b); }
