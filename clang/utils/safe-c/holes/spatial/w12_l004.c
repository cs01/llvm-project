void*memset(void*__sized_by(1) d,int c,unsigned long n);int main(void){char b[4];volatile int k=20;memset(b,1,k);return 0;}
