#include "text.h"
int main(void){ struct text t={0}; if(!text_add(&t,"abc",3)) { text_free(&t); return 1; } struct str s=text_str(&t); text_add(&t,"dddddddddddddddddddddddddddddddd",30); int r = (int)s.p[0]; text_free(&t); return r; }
