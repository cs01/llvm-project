typedef int v4 __attribute__((vector_size(16)));int main(void){v4 a={0};int b[2]={1,2};return b[a[-1]];}
