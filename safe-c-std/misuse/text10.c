#include "text.h"
int main(void){ struct text t={0}; text_add(&t,"abc",3); t.len=5000; struct str s=text_str(&t); int r=(int)s.len; text_add(&t,"x",1); text_free(&t); return r>99; }
