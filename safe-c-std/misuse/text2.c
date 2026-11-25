#include "text.h"
int main(void){ struct text t={0}; text_add(&t,"abc",3); struct str s=text_str(&t); text_free(&t); return s.len?(int)s.p[0]:0; }
