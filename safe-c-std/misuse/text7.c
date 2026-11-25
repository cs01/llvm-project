#include "text.h"
int main(void){ struct text t={0}; text_add(&t,"abc",3); struct text u=t; text_free(&t); text_free(&u); return 0; }
