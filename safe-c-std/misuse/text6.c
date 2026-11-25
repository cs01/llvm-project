#include "text.h"
int main(void){ struct str s=STR("abc"); s.len=99; return s.p[50]; }
