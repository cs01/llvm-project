typedef int v4 __attribute__((vector_size(16)));struct S{v4 a;int k;};int main(void){struct S s={{0},42};return s.a[4];}
