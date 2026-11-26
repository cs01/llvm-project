#include "text.h"
int main(void){ struct strings n = {0}; (void)strings_add(&n, STR("a")); struct str f = strings_at(&n, 0); strings_free(&n); return f.len ? f.p[0] : 0; }
