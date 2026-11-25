// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
__attribute__((format_arg(1))) static const char*tr(const char*s){ return "%s\n"; }
int main(void){ printf(tr("%d\n"),5); }
