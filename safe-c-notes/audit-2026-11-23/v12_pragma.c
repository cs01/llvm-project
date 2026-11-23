#pragma clang attribute push(__attribute__((no_sanitize("null","bounds"))), apply_to = function)
struct Big { char pad[1 << 30]; };
int main(int argc, char **argv) { struct Big *slot[1]; slot[0]->pad[0x41414141] = 1; return 0; }
#pragma clang attribute pop
