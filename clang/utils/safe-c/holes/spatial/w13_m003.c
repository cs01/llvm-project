struct S{int x[2];int k;};int main(void){struct S s={{1,2},7};volatile int i=2;__builtin_nontemporal_store(42,s.x+i);return s.k;}
