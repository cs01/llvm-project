void*memcpy(void*__sized_by(1) d,const void*__sized_by(1) s,unsigned long n);int main(void){char b[4],c[20]={0};volatile int k=20;memcpy(b,c,k);return 0;}
