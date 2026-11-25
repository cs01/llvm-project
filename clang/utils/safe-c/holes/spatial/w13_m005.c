int main(void){char b[8]={0};volatile int i=100000;__builtin_nontemporal_store(42,b+i);return 0;}
