#include "text.h"
int main(void){ struct strings n = {0}; (void)strings_add(&n, STR("ab")); struct str f = strings_at(&n, 0); volatile size_t k = 9; int r = f.p[k]; strings_free(&n); return r; }
