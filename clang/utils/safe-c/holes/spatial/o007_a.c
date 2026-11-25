struct T{long a;};int f(struct T(*)[1]);int main(void){struct T s[1]={{0x41414141}};return f(&s);}
