#include "text.h"
int main(void){ str_long r = str_int(STR("x")); switch (r.ok) { case true: return (int)r.value; } return 0; }
