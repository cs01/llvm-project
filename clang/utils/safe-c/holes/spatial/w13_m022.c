typedef int v4 __attribute__((vector_size(16)));struct S{v4 a;int k;};int main(void){struct S s={{0},7};s.a[4]=42;return s.k;}
