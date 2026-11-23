extern int tbl2[1000];
int main(int argc, char **argv) { tbl2[900 + argc] = 0x56565656; return 0; }
