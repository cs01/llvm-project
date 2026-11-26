#include "text.h"
int main(void){ struct strings n = {0}; for (int i = 0; i < 50; i++) if (!strings_add(&n, STR("ab"))) return 1; struct str f = strings_at(&n, 49); int r = f.len == 2 && f.p[1] == 98 ? 0 : 1; strings_free(&n); return r; }
