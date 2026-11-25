#include "file.h"
int main(void){ struct text t = {0}; file_write("/nonexistent/x", text_str(&t)); text_free(&t); return 0; }
