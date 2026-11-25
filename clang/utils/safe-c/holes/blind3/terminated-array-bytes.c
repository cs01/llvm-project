#include <string.h>
struct s { char g[__null_terminated 4]; };
int main(void) { struct s x = {"abc"}; memset(&x, 'x', sizeof x); return strlen(x.g); }
