void bzero(void*,unsigned long);struct S{char b[4];char k[4];};int main(void){struct S s={{1},{1}};volatile int k=8;bzero(s.b,k);return s.k[0]?0:42;}
