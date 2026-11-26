#include "text.h"
int main(void){ struct strings n = {0}; struct strings m = n; strings_free(&n); strings_free(&m); return 0; }
