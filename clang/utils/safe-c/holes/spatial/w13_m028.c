typedef int v4 __attribute__((vector_size(16)));__attribute__((noinline)) void f(v4*a){(*a)[4]=42;}int main(void){v4 a[2]={{0},{1,0}};f(a);return a[1][0];}
