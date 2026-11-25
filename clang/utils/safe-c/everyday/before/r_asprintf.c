// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(void){ char*s; if(asprintf(&s,"%d",4)>0){ puts(s); free(s);} }
