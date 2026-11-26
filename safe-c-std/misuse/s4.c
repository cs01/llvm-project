#include "text.h"
int main(void){ struct strings n = {0}; (void)strings_add(&n, strings_at(&n, 0)); strings_free(&n); return 0; }
