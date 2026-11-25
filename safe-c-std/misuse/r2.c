#include "text.h"
int main(void){ str_long r = str_int(STR("x")); if (r.ok) return 0; return (int)r.value; }
