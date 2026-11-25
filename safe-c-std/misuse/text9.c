#include "text.h"
int main(void){ struct text t={0}; text_add(&t,"abc",3); struct str s=text_str(&t); text_add(&t,s.p,s.len); text_free(&t); return 0; }
