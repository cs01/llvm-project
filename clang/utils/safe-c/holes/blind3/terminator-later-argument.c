#include <string.h>
int main(void) { char b[4] = "abc"; return strcmp(b, (b[3] = 'x', "abcxy")); }
