#include "text.h"
int main(void){ struct text t={0}; volatile size_t n=9; text_add(&t,"abc",n); text_free(&t); return 0; }
