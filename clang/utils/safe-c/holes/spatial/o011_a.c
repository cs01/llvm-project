typedef struct{long a;}T;struct S{T*_Nullable t;};int f(struct S*);int main(void){T t={0x41414141};struct S s={&t};return f(&s);}
