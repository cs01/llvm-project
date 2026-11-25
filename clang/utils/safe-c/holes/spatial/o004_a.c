struct T{long a;};struct S{struct T*_Nullable t;};int f(struct S*);int main(void){struct T q={0x41414141};struct S s={&q};return f(&s);}
