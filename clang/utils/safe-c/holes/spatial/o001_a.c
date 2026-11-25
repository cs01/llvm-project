struct T{int a;};struct S{struct T*_Nullable t;};int f(struct S*);int main(void){struct T q[1]={{1}};struct S s={q};return f(&s);}
