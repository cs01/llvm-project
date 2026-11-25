typedef int v4 __attribute__((vector_size(16)));__attribute__((noinline)) int f(v4*a){return (*a)[4];}int main(void){v4 a[2]={{0},{42,0}};return f(a);}
