#include "text.h"
int main(void){ struct str s=STR("abc"); volatile size_t i=3; return s.p[i+1]; }
