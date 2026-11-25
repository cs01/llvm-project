struct S{const char*__null_terminated p;};int f(struct S*);int main(void){struct S s={"a"};return f(&s);}
