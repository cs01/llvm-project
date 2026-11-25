void*memset(void*__sized_by(1) d,int c,unsigned long n);struct S{char b[4];char k[4];};int main(void){struct S s={{1},{1}};volatile int k=8;memset(s.b,42,k);return s.k[0];}
