// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(void){ char s[]="a b c"; char*save; for(char*t=strtok_r(s," ",&save);t;t=strtok_r(NULL," ",&save)) puts(t); }
