#include "file.h"
int main(void){ struct text t = {0}; file_result r = file_read("/nonexistent", &t); int e = r.ok ? 0 : r.err; size_t n = r.ok ? r.value : 0; text_free(&t); return e + (int)n; }
